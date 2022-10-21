#include "ef_vi_internal.h"
#include "logging.h"
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <rte_ring.h>

/*
 The names of the rings must match what is in the DPDK primary process
 */
static const char *_TX_RING = "TX_RING";
static const char *_TX_COMP_RING = "TX_COMP_RING";
static const char *_RX_RING = "RX_RING";
static const char *_RX_FILL_RING = "RX_FILL_RING";
static const char *_RX_MBUF_POOL = "RX_MBUF_POOL";
static const char *_TX_PREP_RING = "TX_PREP_RING";
static const char *_RX_PREP_RING = "RX_PREP_RING";

typedef struct dpdk_rings
{
  struct rte_mempool *mempool;
  struct rte_ring *rx_fill_ring;
  struct rte_ring *rx_ring;
  struct rte_ring *tx_ring;
  struct rte_ring *tx_comp_ring;
  struct rte_ring *tx_prep_ring;
  struct rte_ring *rx_prep_ring;
} dpdk_rings;

static dpdk_rings m_rings;

static struct rte_mbuf *pending_evs[16]; // 16 is the exact size of the event request

void efdpdk_drain_ring(struct rte_ring *ring)
{
  unsigned available = 0;
  int count = 0;
  int burst_size = 32;
  struct rte_mbuf *bufs[burst_size];

  do
  {
    count = rte_ring_dequeue_burst(ring, (void **)&bufs[0], burst_size, &available);
    rte_pktmbuf_free_bulk(bufs, count);
  } while (available != 0);
}

void efdpdk_drain_rings(void)
{
  efdpdk_drain_ring(m_rings.rx_fill_ring);
  efdpdk_drain_ring(m_rings.rx_ring);
  efdpdk_drain_ring(m_rings.rx_prep_ring);
  efdpdk_drain_ring(m_rings.tx_prep_ring);
  efdpdk_drain_ring(m_rings.tx_comp_ring);
  efdpdk_drain_ring(m_rings.tx_ring);
}

int efdpdk_init_rings(void)
{
  m_rings.mempool = rte_mempool_lookup(_RX_MBUF_POOL);
  m_rings.rx_fill_ring = rte_ring_lookup(_RX_FILL_RING);
  m_rings.rx_ring = rte_ring_lookup(_RX_RING);
  m_rings.tx_ring = rte_ring_lookup(_TX_RING);
  m_rings.tx_comp_ring = rte_ring_lookup(_TX_COMP_RING);
  m_rings.tx_prep_ring = rte_ring_lookup(_TX_PREP_RING);
  m_rings.rx_prep_ring = rte_ring_lookup(_RX_PREP_RING);
  if (m_rings.mempool == NULL)
  {
    ef_log("NO MEMPOOL");
    return -1;
  }
  if (m_rings.tx_ring == NULL)
  {
    ef_log("NO TX RING");
    return -1;
  }
  if (m_rings.tx_comp_ring == NULL)
  {
    ef_log("NO TX COMP RING");
    return -1;
  }
  if (m_rings.rx_fill_ring == NULL)
  {
    ef_log("NO FILL RING");
    return -1;
  }
  if (m_rings.rx_ring == NULL)
  {
    ef_log("NO RX RING");
    return -1;
  }
  if (m_rings.tx_prep_ring == NULL)
  {
    ef_log("NO PREP RING");
    return -1;
  }
  if (m_rings.rx_prep_ring == NULL)
  {
    ef_log("NO RX PREP RING");
    return -1;
  }

  efdpdk_drain_rings();

  return 0;
}

static void efdpdk_ef_vi_tx_fill_pkt(ef_vi *vi, const char *pkt, const unsigned len)
{
  struct rte_mbuf *mbufs[1];
  if (rte_mempool_get_bulk(m_rings.mempool, (void **)mbufs, 1) == 0)
  {
    memcpy(rte_pktmbuf_mtod(mbufs[0], char *), pkt, len);
    mbufs[0]->data_len = len;
    rte_ring_enqueue_bulk(m_rings.tx_prep_ring, (void *)mbufs, 1, NULL);
  }
  else
  {
    ef_log("failed to fill tx packet. Out of buffers");
  }
}

static int
efdpdk_ef_vi_transmitv_init(ef_vi *vi, const ef_iovec *iov,
                            int iov_len, ef_request_id dma_id)
{
  ef_vi_txq *q = &vi->vi_txq;
  ef_vi_txq_state *qs = &vi->ep_state->txq;
  int i;
  if (iov_len != 1)
    return -EINVAL; /* Multiple buffers per packet not supported */

  if (qs->added - qs->removed >= q->mask)
    return -EAGAIN;

  i = qs->added++ & q->mask;
  EF_VI_BUG_ON(q->ids[i] != EF_REQUEST_ID_MASK);
  q->ids[i] = dma_id;
  return 0;
}

static void efdpdk_ef_vi_transmit_push(ef_vi *vi)
{
  struct rte_mbuf *mbufs[EF_VI_TRANSMIT_BATCH];
  int removed;

  removed = rte_ring_dequeue_burst(m_rings.tx_prep_ring, (void **)mbufs, EF_VI_TRANSMIT_BATCH, NULL);
  if (removed != 0)
  {
    rte_ring_enqueue_bulk(m_rings.tx_ring, (void **)mbufs, removed, NULL);
  }
  else
  {
    ef_log("pushing but have no data");
  }
}

static int efdpdk_ef_vi_transmit(ef_vi *vi, ef_addr base, int len,
                                 ef_request_id dma_id)
{
  ef_iovec iov = {base, len};
  int rc = efdpdk_ef_vi_transmitv_init(vi, &iov, 1, dma_id);
  if (rc == 0)
  {
    wmb();
    efdpdk_ef_vi_transmit_push(vi);
  }
  return rc;
}

static int efdpdk_ef_vi_transmitv(ef_vi *vi, const ef_iovec *iov, int iov_len,
                                  ef_request_id dma_id)
{
  int rc = efdpdk_ef_vi_transmitv_init(vi, iov, iov_len, dma_id);
  if (rc == 0)
  {
    wmb();
    efdpdk_ef_vi_transmit_push(vi);
  }
  return rc;
}

static int efdpdk_ef_vi_transmit_pio(ef_vi *vi, int offset, int len,
                                     ef_request_id dma_id)
{
  return -EOPNOTSUPP;
}

static int efdpdk_ef_vi_transmit_copy_pio(ef_vi *vi, int offset,
                                          const void *src_buf, int len,
                                          ef_request_id dma_id)
{
  return -EOPNOTSUPP;
}

static void efdpdk_ef_vi_transmit_pio_warm(ef_vi *vi)
{
  /* PIO is unsupported so do nothing */
}

static void efdpdk_ef_vi_transmit_copy_pio_warm(ef_vi *vi, int pio_offset,
                                                const void *src_buf, int len)
{
  /* PIO is unsupported so do nothing */
}

static void efdpdk_ef_vi_transmitv_ctpio(ef_vi *vi, size_t frame_len,
                                         const struct iovec *iov, int iovcnt,
                                         unsigned threshold)
{
  /* CTPIO is unsupported so do nothing. Fallback will send the packet. */
}

static void efdpdk_ef_vi_transmitv_ctpio_copy(ef_vi *vi, size_t frame_len,
                                              const struct iovec *iov, int iovcnt,
                                              unsigned threshold, void *fallback)
{
  // TODO copy to fallback
}

static int efdpdk_ef_vi_transmit_ctpio_fallback(ef_vi *vi, ef_addr dma_addr,
                                                size_t len, ef_request_id dma_id)
{
  EF_VI_ASSERT(vi->vi_flags & EF_VI_TX_CTPIO);
  return efdpdk_ef_vi_transmit(vi, dma_addr, len, dma_id);
}

static int efdpdk_ef_vi_transmitv_ctpio_fallback(ef_vi *vi,
                                                 const ef_iovec *dma_iov,
                                                 int dma_iov_len,
                                                 ef_request_id dma_id)
{
  EF_VI_ASSERT(vi->vi_flags & EF_VI_TX_CTPIO);
  return efdpdk_ef_vi_transmitv(vi, dma_iov, dma_iov_len, dma_id);
}

static int efdpdk_ef_vi_transmit_ctpio_fallback_not_supp(ef_vi *vi,
                                                         ef_addr dma_addr,
                                                         size_t len,
                                                         ef_request_id dma_id)
{
  return -EOPNOTSUPP;
}

static int efdpdk_ef_vi_transmitv_ctpio_fallback_not_supp(ef_vi *vi,
                                                          const ef_iovec *dma_iov,
                                                          int dma_iov_len,
                                                          ef_request_id dma_id)
{
  return -EOPNOTSUPP;
}

static int efdpdk_ef_vi_transmit_alt_select(ef_vi *vi, unsigned alt_id)
{
  return -EOPNOTSUPP;
}

static int efdpdk_ef_vi_transmit_alt_select_normal(ef_vi *vi)
{
  return -EOPNOTSUPP;
}

static int efdpdk_ef_vi_transmit_alt_stop(ef_vi *vi, unsigned alt_id)
{
  return -EOPNOTSUPP;
}

static int efdpdk_ef_vi_transmit_alt_discard(ef_vi *vi, unsigned alt_id)
{
  return -EOPNOTSUPP;
}

static int efdpdk_ef_vi_transmit_alt_go(ef_vi *vi, unsigned alt_id)
{
  return -EOPNOTSUPP;
}

static ssize_t efdpdk_ef_vi_transmit_memcpy(struct ef_vi *vi,
                                            const ef_remote_iovec *dst_iov,
                                            int dst_iov_len,
                                            const ef_remote_iovec *src_iov,
                                            int src_iov_len)
{
  return -EOPNOTSUPP;
}

static int efdpdk_ef_vi_transmit_memcpy_sync(struct ef_vi *vi,
                                             ef_request_id dma_id)
{
  return -EOPNOTSUPP;
}

static int efdpdk_ef_vi_receive_init(ef_vi *vi, ef_addr addr,
                                     ef_request_id dma_id)
{
  ef_vi_rxq *q = &vi->vi_rxq;
  ef_vi_rxq_state *qs = &vi->ep_state->rxq;
  struct rte_mbuf *mbufs[1];
  int i;
  if (qs->added - qs->removed >= q->mask)
    return -EAGAIN;

  i = qs->added++ & q->mask;
  q->ids[i] = dma_id;

  if (rte_mempool_get_bulk(m_rings.mempool, (void **)mbufs, 1) == 0)
  {
    rte_ring_enqueue_bulk(m_rings.rx_prep_ring, (void *)mbufs, 1, NULL);
  }

  return 0;
}

static void efdpdk_ef_vi_receive_push(ef_vi *vi)
{
  int size = rte_ring_get_capacity(m_rings.rx_prep_ring);
  struct rte_mbufs *mbufs[size];
  int dequeued = rte_ring_dequeue_burst(m_rings.rx_prep_ring, (void **)&mbufs[0], size, NULL);
  if (dequeued == 0)
  {
    ef_log("failed to get rx prep ring buffs");
    return;
  }
  if (rte_ring_enqueue_bulk(m_rings.rx_fill_ring, (void **)&mbufs[0], dequeued, NULL) == 0)
  {
    ef_log("failed to enqueue on the fill ring");
  }
}

static void efdpdk_ef_eventq_prime(ef_vi *vi)
{
  // TODO
}

static void efdpdk_ev_fill_pkt(ef_vi *vi, char *pkt, int index)
{
  struct rte_mbuf *mbuf = pending_evs[index];
  memcpy(pkt, rte_pktmbuf_mtod(mbuf, unsigned char *), rte_pktmbuf_data_len(mbuf));
  rte_pktmbuf_free(mbuf);
}

static int efdpdk_ef_eventq_poll(ef_vi *vi, ef_event *evs, int evs_len)
{
  // n is the index of the current event we're filling in. consists of both RX events and TX completion events
  int n = 0, count = 0;
  unsigned available = 0, page = 0;

  ef_vi_rxq *rx_q = &vi->vi_rxq;
  ef_vi_txq *tx_q = &vi->vi_txq;
  ef_vi_rxq_state *rx_qs = &vi->ep_state->rxq;
  ef_vi_txq_state *tx_qs = &vi->ep_state->txq;
  struct rte_mbuf *rx_bufs[evs_len];
  struct rte_mbuf *tx_bufs[EF_VI_TRANSMIT_BATCH];
  do
  {
    count = rte_ring_dequeue_burst(m_rings.rx_ring, (void **)rx_bufs, evs_len, &available);
    for (int i = 0; i < count; i++)
    {
      unsigned desc_i = rx_qs->removed++ & rx_q->mask;
      evs[n].rx.type = EF_EVENT_TYPE_RX;
      evs[n].rx.q_id = 0;
      evs[n].rx.rq_id = rx_q->ids[desc_i]; // dma_id

      // mark the descriptor reusable
      rx_q->ids[desc_i] = EF_REQUEST_ID_MASK; /* Debug only? */

      evs[n].rx.flags = EF_EVENT_FLAG_SOP;
      evs[n].rx.len = rte_pktmbuf_data_len(rx_bufs[i]);
      evs[n].rx.ofs = 0;

      pending_evs[n] = rx_bufs[i];
      ++n;
    }

  } while (available != 0 && n < evs_len);

  if (n < evs_len)
  {
    available = 0;
    page = 0;
    do
    {
      // TX can acknowledge multiple at once using the last id
      count = rte_ring_dequeue_burst(m_rings.tx_comp_ring, (void **)tx_bufs, EF_VI_TRANSMIT_BATCH, &available);
      if (count != 0)
      {
        int desc_id = (tx_qs->removed + count + (EF_VI_TRANSMIT_BATCH * page)) & tx_q->mask;
        evs[n].tx.type = EF_EVENT_TYPE_TX;
        evs[n].tx.desc_id = desc_id;
        evs[n].tx.flags = 0;
        evs[n].tx.q_id = 0;
        ++n;
        ++page;
        rte_pktmbuf_free_bulk(tx_bufs, count);
      }
    } while (available != 0 && n < evs_len);
  }

  return n;
}

static void efdpdk_ef_eventq_timer_prime(ef_vi *vi, unsigned v)
{
  // TODO
}

static void efdpdk_ef_eventq_timer_run(ef_vi *vi, unsigned v)
{
  // TODO
}

static void efdpdk_ef_eventq_timer_clear(ef_vi *vi)
{
  // TODO
}

static void efdpdk_ef_eventq_timer_zero(ef_vi *vi)
{
  // TODO
}

int efdpdk_ef_eventq_check_event(const ef_vi *_vi)
{
  return rte_ring_count(m_rings.rx_ring) > 0 || rte_ring_count(m_rings.tx_comp_ring) > 0;
}

void efdpdk_vi_init(ef_vi *vi)
{
  efdpdk_init_rings();
  vi->ops.transmit = efdpdk_ef_vi_transmit;
  vi->ops.transmitv = efdpdk_ef_vi_transmitv;
  vi->ops.transmitv_init = efdpdk_ef_vi_transmitv_init;
  vi->ops.transmit_push = efdpdk_ef_vi_transmit_push;
  vi->ops.transmit_pio = efdpdk_ef_vi_transmit_pio;
  vi->ops.transmit_copy_pio = efdpdk_ef_vi_transmit_copy_pio;
  vi->ops.transmit_pio_warm = efdpdk_ef_vi_transmit_pio_warm;
  vi->ops.transmit_copy_pio_warm = efdpdk_ef_vi_transmit_copy_pio_warm;
  vi->ops.transmitv_ctpio = efdpdk_ef_vi_transmitv_ctpio;
  vi->ops.transmitv_ctpio_copy = efdpdk_ef_vi_transmitv_ctpio_copy;
  vi->ops.transmit_alt_select = efdpdk_ef_vi_transmit_alt_select;
  vi->ops.transmit_alt_select_default = efdpdk_ef_vi_transmit_alt_select_normal;
  vi->ops.transmit_alt_stop = efdpdk_ef_vi_transmit_alt_stop;
  vi->ops.transmit_alt_go = efdpdk_ef_vi_transmit_alt_go;
  vi->ops.transmit_alt_discard = efdpdk_ef_vi_transmit_alt_discard;
  vi->ops.receive_init = efdpdk_ef_vi_receive_init;
  vi->ops.receive_push = efdpdk_ef_vi_receive_push;
  vi->ops.eventq_poll = efdpdk_ef_eventq_poll;
  vi->ops.eventq_prime = efdpdk_ef_eventq_prime;
  vi->ops.eventq_timer_prime = efdpdk_ef_eventq_timer_prime;
  vi->ops.eventq_timer_run = efdpdk_ef_eventq_timer_run;
  vi->ops.eventq_timer_clear = efdpdk_ef_eventq_timer_clear;
  vi->ops.eventq_timer_zero = efdpdk_ef_eventq_timer_zero;
  vi->ops.transmit_memcpy = efdpdk_ef_vi_transmit_memcpy;
  vi->ops.transmit_memcpy_sync = efdpdk_ef_vi_transmit_memcpy_sync;
  vi->ops.ev_fill_pkt = efdpdk_ev_fill_pkt;
  vi->ops.tx_fill_pkt = efdpdk_ef_vi_tx_fill_pkt;
  if (vi->vi_flags & EF_VI_TX_CTPIO)
  {
    vi->ops.transmit_ctpio_fallback = efdpdk_ef_vi_transmit_ctpio_fallback;
    vi->ops.transmitv_ctpio_fallback = efdpdk_ef_vi_transmitv_ctpio_fallback;
  }
  else
  {
    vi->ops.transmit_ctpio_fallback =
        efdpdk_ef_vi_transmit_ctpio_fallback_not_supp;
    vi->ops.transmitv_ctpio_fallback =
        efdpdk_ef_vi_transmitv_ctpio_fallback_not_supp;
  }

  vi->rx_buffer_len = 2048;
  vi->rx_prefix_len = 0;
  vi->evq_phase_bits = 1; /* We set this flag for ef_eventq_has_event */
  vi->nic_type.arch = EF_VI_ARCH_DPDK;
  ef_log("DPDK VI INITED");
}
