#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "e1000_dev.h"
#include "net.h"

#define TX_RING_SIZE 16
static struct tx_desc tx_ring[TX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *tx_mbufs[TX_RING_SIZE];

#define RX_RING_SIZE 16
static struct rx_desc rx_ring[RX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *rx_mbufs[RX_RING_SIZE];

// remember where the e1000's registers live.
static volatile uint32 *regs;

struct spinlock e1000_lock;

// called by pci_init().
// xregs is the memory address at which the
// e1000's registers are mapped.
void
e1000_init(uint32 *xregs)
{
  int i;

  initlock(&e1000_lock, "e1000");

  regs = xregs;

  // Reset the device
  regs[E1000_IMS] = 0; // disable interrupts
  regs[E1000_CTL] |= E1000_CTL_RST;
  regs[E1000_IMS] = 0; // redisable interrupts
  __sync_synchronize();

  // [E1000 14.5] Transmit initialization
  memset(tx_ring, 0, sizeof(tx_ring));
  for (i = 0; i < TX_RING_SIZE; i++) {
    tx_ring[i].status = E1000_TXD_STAT_DD;
    tx_mbufs[i] = 0;
  }
  regs[E1000_TDBAL] = (uint64) tx_ring;
  if(sizeof(tx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_TDLEN] = sizeof(tx_ring);
  regs[E1000_TDH] = regs[E1000_TDT] = 0;
  
  // [E1000 14.4] Receive initialization
  memset(rx_ring, 0, sizeof(rx_ring));
  for (i = 0; i < RX_RING_SIZE; i++) {
    rx_mbufs[i] = mbufalloc(0);
    if (!rx_mbufs[i])
      panic("e1000");
    rx_ring[i].addr = (uint64) rx_mbufs[i]->head;
  }
  regs[E1000_RDBAL] = (uint64) rx_ring;
  if(sizeof(rx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_RDH] = 0;
  regs[E1000_RDT] = RX_RING_SIZE - 1;
  regs[E1000_RDLEN] = sizeof(rx_ring);

  // filter by qemu's MAC address, 52:54:00:12:34:56
  regs[E1000_RA] = 0x12005452;
  regs[E1000_RA+1] = 0x5634 | (1<<31);
  // multicast table
  for (int i = 0; i < 4096/32; i++)
    regs[E1000_MTA + i] = 0;

  // transmitter control bits.
  regs[E1000_TCTL] = E1000_TCTL_EN |  // enable
    E1000_TCTL_PSP |                  // pad short packets
    (0x10 << E1000_TCTL_CT_SHIFT) |   // collision stuff
    (0x40 << E1000_TCTL_COLD_SHIFT);
  regs[E1000_TIPG] = 10 | (8<<10) | (6<<20); // inter-pkt gap

  // receiver control bits.
  regs[E1000_RCTL] = E1000_RCTL_EN | // enable receiver
    E1000_RCTL_BAM |                 // enable broadcast
    E1000_RCTL_SZ_2048 |             // 2048-byte rx buffers
    E1000_RCTL_SECRC;                // strip CRC
  
  // ask e1000 for receive interrupts.
  regs[E1000_RDTR] = 0; // interrupt after every received packet (no timer)
  regs[E1000_RADV] = 0; // interrupt after every packet (no timer)
  regs[E1000_IMS] = (1 << 7); // RXDW -- Receiver Descriptor Write Back
}

int
e1000_transmit(struct mbuf *m)
{
  acquire(&e1000_lock); // 上锁以保证操作的原子性

  int index = regs[E1000_TDT]; // 读取当前环形缓冲区（TX ring）的索引
  
  // 检查当前描述符是否可用，即检查 E1000_TXD_STAT_DD 标志位是否被设置
  if(!(tx_ring[index].status & E1000_TXD_STAT_DD)){
    release(&e1000_lock);
    return -1; // 如果不可用，返回 -1 表示发送失败
  }
  
  // 如果存在旧的 mbuf，调用 mbuffree() 释放它，避免内存泄漏
  if(tx_mbufs[index])
    mbuffree(tx_mbufs[index]);
  
  // 将当前 mbuf 存储到 tx_mbufs 数组中，以便稍后释放
  tx_mbufs[index] = m;

  // 将数据包的地址（m->head）和长度（m->len）写入描述符
  tx_ring[index].addr = (uint64)m->head;
  tx_ring[index].length = m->len;

  // 设置描述符的控制命令标志（cmd），确保 E1000 发送这个数据包
  tx_ring[index].cmd = E1000_TXD_CMD_RS | E1000_TXD_CMD_EOP;
  tx_ring[index].status = 0; // 清除状态，以便硬件更新

  // 更新环形缓冲区的 TDT（发射描述符尾指针），以指向下一个描述符
  regs[E1000_TDT] = (index + 1) % TX_RING_SIZE;

  release(&e1000_lock); // 解锁

  return 0; // 成功发送数据包，返回 0
}

static void
e1000_recv(void)
{
  // 无限循环，用于持续检查和处理接收到的包
  while(1){
    // 计算下一个要处理的接收描述符的索引位置
    // E1000_RDT 是接收描述符尾指针，指向上一个处理完毕的位置
    // 加 1 并取模是为了获取下一个描述符的位置
    int index = (regs[E1000_RDT] + 1) % RX_RING_SIZE;

    // 检查当前描述符是否已被硬件填充（即检查 DD 状态位是否被设置）
    // 如果未设置，说明该描述符没有可用的数据包，返回不做处理
    if(!(rx_ring[index].status & E1000_RXD_STAT_DD))
      return;
    
    // 设置当前 `mbuf` 的长度为描述符中的数据包长度
    rx_mbufs[index]->len = rx_ring[index].length;

    // 将接收到的 `mbuf` 交给网络栈进行处理
    net_rx(rx_mbufs[index]);

    // 为接收描述符分配一个新的 `mbuf` 以接收下一个数据包
    rx_mbufs[index] = mbufalloc(0); 
    if(!rx_mbufs[index])
      panic("e1000");
      
    // 清除当前描述符的状态，以便硬件重新使用它
    rx_ring[index].status = 0;

    // 将新的 `mbuf` 地址设置为描述符的地址字段
    // 确保下次接收数据包时硬件知道要将数据写入哪里
    rx_ring[index].addr = (uint64)rx_mbufs[index]->head;
    
    // 更新 E1000_RDT 寄存器，使其指向当前处理的描述符位置
    // 这告诉硬件已经处理完这个描述符，可以继续接收新的数据包
    regs[E1000_RDT] = index;
  }

}

void
e1000_intr(void)
{
  // tell the e1000 we've seen this interrupt;
  // without this the e1000 won't raise any
  // further interrupts.
  regs[E1000_ICR] = 0xffffffff;

  e1000_recv();
}
