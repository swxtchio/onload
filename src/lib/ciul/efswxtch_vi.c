#include "ef_vi_internal.h"
#include "logging.h"
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <rte_ring.h>

/*
 The names of the rings must match what is in the swxtch primary process
 */
static const char *_TX_RING = "TX_RING";
static const char *_TX_COMP_RING = "TX_COMP_RING";
static const char *_RX_RING = "RX_RING";
static const char *_RX_FILL_RING = "RX_FILL_RING";
static const char *_RX_MBUF_POOL = "RX_MBUF_POOL";
static const char *_TX_PREP_RING = "TX_PREP_RING";
static const char *_RX_PREP_RING = "RX_PREP_RING";
static const char *_RX_PENDING_RING = "RX_PENDING_RING";
static const unsigned RECEIVE_PUSH_SIZE = 16;

typedef struct swxtch_rings {
  struct rte_mempool *mempool;       // basic mempool to pull mbufs from
  struct rte_ring *rx_fill_ring;     // rx fill ring to fill indicate to that
                                     // rx packets can be received
  struct rte_ring *rx_ring;          // ring containing rx packets
  struct rte_ring *tx_ring;          // ring contain packets to send
  struct rte_ring *tx_comp_ring;     // ring containing packets that were sent
  // the prep rings are semi hacky and are used as a staging area for
  // rx|tx_init before _push is called
  struct rte_ring *tx_prep_ring;
  struct rte_ring *rx_prep_ring;
  // in between polling and reading rx packets live here
  struct rte_ring *rx_pending_ring;
} swxtch_rings;

static swxtch_rings m_rings;

// testing using this as our "fill pkt"
static int fake_fill_pkt = 1;

// drain a ring on startup to free any stuck mbufs
void efswxtch_drain_ring(struct rte_ring *ring, int should_free)
{
  unsigned available = 0;
  int count = 0;
  int burst_size = 32;
  void *bufs[burst_size];

  do {
    count = rte_ring_dequeue_burst(ring, &bufs[0], burst_size, &available);

    if( should_free ) {
      rte_pktmbuf_free_bulk((struct rte_mbuf **) bufs, count);
    }
  } while( available != 0 );
}

void efswxtch_drain_rings(void)
{
  efswxtch_drain_ring(m_rings.rx_fill_ring, 0);
  efswxtch_drain_ring(m_rings.tx_ring, 1);
  efswxtch_drain_ring(m_rings.rx_ring, 1);
  efswxtch_drain_ring(m_rings.rx_prep_ring, 0);
  efswxtch_drain_ring(m_rings.tx_prep_ring, 1);
  efswxtch_drain_ring(m_rings.tx_comp_ring, 1);
  efswxtch_drain_ring(m_rings.rx_pending_ring, 1);
}

int efswxtch_init_rings(void)
{
  m_rings.mempool = rte_mempool_lookup(_RX_MBUF_POOL);
  m_rings.rx_fill_ring = rte_ring_lookup(_RX_FILL_RING);
  m_rings.rx_ring = rte_ring_lookup(_RX_RING);
  m_rings.tx_ring = rte_ring_lookup(_TX_RING);
  m_rings.tx_comp_ring = rte_ring_lookup(_TX_COMP_RING);
  m_rings.tx_prep_ring = rte_ring_lookup(_TX_PREP_RING);
  m_rings.rx_prep_ring = rte_ring_lookup(_RX_PREP_RING);
  m_rings.rx_pending_ring = rte_ring_lookup(_RX_PENDING_RING);
  if( m_rings.mempool == NULL ) {
    ef_log("NO MEMPOOL");
    return -1;
  }
  if( m_rings.tx_ring == NULL ) {
    ef_log("NO TX RING");
    return -1;
  }
  if( m_rings.tx_comp_ring == NULL ) {
    ef_log("NO TX COMP RING");
    return -1;
  }
  if( m_rings.rx_fill_ring == NULL ) {
    ef_log("NO FILL RING");
    return -1;
  }
  if( m_rings.rx_ring == NULL ) {
    ef_log("NO RX RING");
    return -1;
  }
  if( m_rings.tx_prep_ring == NULL ) {
    ef_log("NO PREP RING");
    return -1;
  }
  if( m_rings.rx_prep_ring == NULL ) {
    ef_log("NO RX PREP RING");
    return -1;
  }
  if( m_rings.rx_pending_ring == NULL ) {
    ef_log("NO RX Pending RING");
    return -1;
  }

  efswxtch_drain_rings();

  return 0;
}

// Enqueues as many mbufs as it can. Returns false if not all are enqueued and
// will free any mbufs that aren't enqueued
static int efswxtch_ef_vi_safe_enqueue(
    struct rte_ring *ring, struct rte_mbuf **mbufs, unsigned count)
{
  unsigned enq = rte_ring_enqueue_burst(ring, (void **) mbufs, count, NULL);
  if( enq != count ) {
    ef_log("Only enqueued %d:%d on ring: %s", enq, count, ring->name);
    rte_pktmbuf_free_bulk((struct rte_mbuf **) &mbufs[enq], count - enq);
    return 0;
  }

  return 1;
}

static void efswxtch_ef_vi_tx_fill_pkt(
    ef_vi *vi, const char *pkt, const unsigned len)
{
  struct rte_mbuf *mbufs[1];
  if( rte_mempool_get_bulk(m_rings.mempool, (void **) mbufs, 1) == 0 ) {
    memcpy(rte_pktmbuf_mtod(mbufs[0], char *), pkt, len);
    mbufs[0]->data_len = len;
    efswxtch_ef_vi_safe_enqueue(
        m_rings.tx_prep_ring, (struct rte_mbuf **) mbufs, 1);
  } else {
    ef_log("failed to fill tx packet. Out of buffers");
  }
}

static int efswxtch_ef_vi_transmitv_init(
    ef_vi *vi, const ef_iovec *iov, int iov_len, ef_request_id dma_id)
{
  ef_vi_txq *q = &vi->vi_txq;
  ef_vi_txq_state *qs = &vi->ep_state->txq;
  int i;
  if( iov_len != 1 )
    return -EINVAL; /* Multiple buffers per packet not supported */

  if( qs->added - qs->removed >= q->mask )
    return -EAGAIN;

  i = qs->added++ & q->mask;
  EF_VI_BUG_ON(q->ids[i] != EF_REQUEST_ID_MASK);
  q->ids[i] = dma_id;
  return 0;
}

static void efswxtch_ef_vi_transmit_push(ef_vi *vi)
{
  struct rte_mbuf *mbufs[EF_VI_TRANSMIT_BATCH];
  int removed;

  removed = rte_ring_dequeue_burst(
      m_rings.tx_prep_ring, (void **) mbufs, EF_VI_TRANSMIT_BATCH, NULL);
  if( removed != 0 ) {
    efswxtch_ef_vi_safe_enqueue(m_rings.tx_ring, mbufs, removed);
  } else {
    ef_log("pushing but have no data");
  }
}

static int efswxtch_ef_vi_transmit(
    ef_vi *vi, ef_addr base, int len, ef_request_id dma_id)
{
  ef_iovec iov = { base, len };
  int rc = efswxtch_ef_vi_transmitv_init(vi, &iov, 1, dma_id);
  if( rc == 0 ) {
    wmb();
    efswxtch_ef_vi_transmit_push(vi);
  }
  return rc;
}

static int efswxtch_ef_vi_transmitv(
    ef_vi *vi, const ef_iovec *iov, int iov_len, ef_request_id dma_id)
{
  int rc = efswxtch_ef_vi_transmitv_init(vi, iov, iov_len, dma_id);
  if( rc == 0 ) {
    wmb();
    efswxtch_ef_vi_transmit_push(vi);
  }
  return rc;
}

static int efswxtch_ef_vi_transmit_pio(
    ef_vi *vi, int offset, int len, ef_request_id dma_id)
{
  return -EOPNOTSUPP;
}

static int efswxtch_ef_vi_transmit_copy_pio(
    ef_vi *vi, int offset, const void *src_buf, int len, ef_request_id dma_id)
{
  return -EOPNOTSUPP;
}

static void efswxtch_ef_vi_transmit_pio_warm(ef_vi *vi)
{
  /* PIO is unsupported so do nothing */
}

static void efswxtch_ef_vi_transmit_copy_pio_warm(
    ef_vi *vi, int pio_offset, const void *src_buf, int len)
{
  /* PIO is unsupported so do nothing */
}

static void efswxtch_ef_vi_transmitv_ctpio(ef_vi *vi, size_t frame_len,
    const struct iovec *iov, int iovcnt, unsigned threshold)
{
  /* CTPIO is unsupported so do nothing. Fallback will send the packet. */
}

static void efswxtch_ef_vi_transmitv_ctpio_copy(ef_vi *vi, size_t frame_len,
    const struct iovec *iov, int iovcnt, unsigned threshold, void *fallback)
{
  // TODO copy to fallback
}

static int efswxtch_ef_vi_transmit_ctpio_fallback(
    ef_vi *vi, ef_addr dma_addr, size_t len, ef_request_id dma_id)
{
  EF_VI_ASSERT(vi->vi_flags & EF_VI_TX_CTPIO);
  return efswxtch_ef_vi_transmit(vi, dma_addr, len, dma_id);
}

static int efswxtch_ef_vi_transmitv_ctpio_fallback(
    ef_vi *vi, const ef_iovec *dma_iov, int dma_iov_len, ef_request_id dma_id)
{
  EF_VI_ASSERT(vi->vi_flags & EF_VI_TX_CTPIO);
  return efswxtch_ef_vi_transmitv(vi, dma_iov, dma_iov_len, dma_id);
}

static int efswxtch_ef_vi_transmit_ctpio_fallback_not_supp(
    ef_vi *vi, ef_addr dma_addr, size_t len, ef_request_id dma_id)
{
  return -EOPNOTSUPP;
}

static int efswxtch_ef_vi_transmitv_ctpio_fallback_not_supp(
    ef_vi *vi, const ef_iovec *dma_iov, int dma_iov_len, ef_request_id dma_id)
{
  return -EOPNOTSUPP;
}

static int efswxtch_ef_vi_transmit_alt_select(ef_vi *vi, unsigned alt_id)
{
  return -EOPNOTSUPP;
}

static int efswxtch_ef_vi_transmit_alt_select_normal(ef_vi *vi)
{
  return -EOPNOTSUPP;
}

static int efswxtch_ef_vi_transmit_alt_stop(ef_vi *vi, unsigned alt_id)
{
  return -EOPNOTSUPP;
}

static int efswxtch_ef_vi_transmit_alt_discard(ef_vi *vi, unsigned alt_id)
{
  return -EOPNOTSUPP;
}

static int efswxtch_ef_vi_transmit_alt_go(ef_vi *vi, unsigned alt_id)
{
  return -EOPNOTSUPP;
}

static ssize_t efswxtch_ef_vi_transmit_memcpy(struct ef_vi *vi,
    const ef_remote_iovec *dst_iov, int dst_iov_len,
    const ef_remote_iovec *src_iov, int src_iov_len)
{
  return -EOPNOTSUPP;
}

static int efswxtch_ef_vi_transmit_memcpy_sync(
    struct ef_vi *vi, ef_request_id dma_id)
{
  return -EOPNOTSUPP;
}

static int efswxtch_ef_vi_receive_init(
    ef_vi *vi, ef_addr addr, ef_request_id dma_id)
{
  ef_vi_rxq *q = &vi->vi_rxq;
  ef_vi_rxq_state *qs = &vi->ep_state->rxq;
  int i;
  if( qs->added - qs->removed >= q->mask )
    return -EAGAIN;

  i = qs->added++ & q->mask;
  q->ids[i] = dma_id;

  if( rte_ring_enqueue(m_rings.rx_prep_ring, &fake_fill_pkt) != 0 ) {
    ef_log("Unable to initalize the receive queue");
    return -EAGAIN;
  }

  /*
    if( rte_mempool_get(m_rings.mempool, (void **) &mbufs[0]) == 0 ) {
      if( ! efswxtch_ef_vi_safe_enqueue(m_rings.rx_prep_ring, mbufs, 1) ) {
        return -2;
      }
    } else {
      ef_log("Unable to get mbuf to enqueue on the rx prep ring: %d left",
          rte_mempool_avail_count(m_rings.mempool));
      return -1;
    }
    */

  return 0;
}

static void efswxtch_ef_vi_receive_push(ef_vi *vi)
{
  unsigned available = 0;
  void *fake_pkts[RECEIVE_PUSH_SIZE];
  do {
    int dequeued = rte_ring_dequeue_burst(
        m_rings.rx_prep_ring, fake_pkts, RECEIVE_PUSH_SIZE, &available);

    if( dequeued == 0 ) {
      ef_log("failed to get rx prep ring buffs");
      return;
    }
    if( rte_ring_enqueue_bulk(
            m_rings.rx_fill_ring, fake_pkts, dequeued, NULL) == 0 ) {
      ef_log("Unable to enqueue the fake pkts onto the fill ring");
    }
  } while( available != 0 );
}


static void efswxtch_ef_eventq_prime(ef_vi *vi)
{
  // TODO
}

static void efswxtch_rx_fill_pkt(ef_vi *vi, char *pkt, int index)
{
  struct rte_mbuf *mbuf[1];
  if( rte_ring_dequeue(m_rings.rx_pending_ring, (void **) &mbuf[0]) != 0 ) {
    ef_log("Nothing to fill in the rx pkts");
    return;
  }
  memcpy(pkt, rte_pktmbuf_mtod(mbuf[0], unsigned char *),
      rte_pktmbuf_data_len(mbuf[0]));
  if( mbuf[0] == NULL ) {
    ef_log("freeing NULL");
  } else {
    rte_pktmbuf_free(mbuf[0]);
  }
}

static int efswxtch_ef_eventq_poll(ef_vi *vi, ef_event *evs, int evs_len)
{
  // n is the index of the current event we're filling in. consists of both RX
  // events and TX completion events
  int n = 0, count = 0;
  unsigned available = 0, page = 0;

  ef_vi_rxq *rx_q = &vi->vi_rxq;
  ef_vi_txq *tx_q = &vi->vi_txq;
  ef_vi_rxq_state *rx_qs = &vi->ep_state->rxq;
  ef_vi_txq_state *tx_qs = &vi->ep_state->txq;

  // tx_bufs can be large because we can confirm up EF_VI_TRANSMIT_BATCH per
  // event. We can have at most 1 RX event per RX packet
  struct rte_mbuf *rx_bufs[evs_len];
  struct rte_mbuf *tx_bufs[EF_VI_TRANSMIT_BATCH];

  count = rte_ring_dequeue_burst(
      m_rings.rx_ring, (void **) rx_bufs, evs_len, NULL);
  for( int i = 0; i < count; i++ ) {
    unsigned desc_i = rx_qs->removed & rx_q->mask;
    evs[n].rx.type = EF_EVENT_TYPE_RX;
    evs[n].rx.q_id = 0;
    evs[n].rx.rq_id = rx_q->ids[desc_i];     // dma_id
    ++rx_qs->removed;

    // mark the descriptor reusable
    rx_q->ids[desc_i] = EF_REQUEST_ID_MASK; /* Debug only? */

    evs[n].rx.flags = EF_EVENT_FLAG_SOP;
    evs[n].rx.len = rte_pktmbuf_data_len(rx_bufs[i]);
    evs[n].rx.ofs = 0;
    ++n;
  }

  efswxtch_ef_vi_safe_enqueue(
      m_rings.rx_pending_ring, (struct rte_mbuf **) rx_bufs, count);

  if( n < evs_len ) {
    available = 0;
    page = 0;
    do {
      // TX can acknowledge multiple at once using the last id
      count = rte_ring_dequeue_burst(m_rings.tx_comp_ring, (void **) tx_bufs,
          EF_VI_TRANSMIT_BATCH, &available);
      if( count != 0 ) {
        int desc_id =
            (tx_qs->removed + count + (EF_VI_TRANSMIT_BATCH * page)) &
            tx_q->mask;
        evs[n].tx.type = EF_EVENT_TYPE_TX;
        evs[n].tx.desc_id = desc_id;
        evs[n].tx.flags = 0;
        evs[n].tx.q_id = 0;
        ++n;
        ++page;

        rte_pktmbuf_free_bulk(tx_bufs, count);
      }
    } while( available != 0 && n < evs_len );
  }

  return n;
}

static void efswxtch_ef_eventq_timer_prime(ef_vi *vi, unsigned v)
{
  // TODO
}

static void efswxtch_ef_eventq_timer_run(ef_vi *vi, unsigned v)
{
  // TODO
}

static void efswxtch_ef_eventq_timer_clear(ef_vi *vi)
{
  // TODO
}

static void efswxtch_ef_eventq_timer_zero(ef_vi *vi)
{
  // TODO
}

int efswxtch_ef_eventq_check_event(const ef_vi *_vi)
{
  return rte_ring_count(m_rings.rx_ring) > 0 ||
         rte_ring_count(m_rings.tx_comp_ring) > 0;
}

static int efswxtch_ef_vi_refill_rx(ef_vi *vi)
{
  unsigned existing_count = rte_ring_count(m_rings.rx_fill_ring);
  unsigned count_needed =
      vi->ep_state->rxq.added - vi->ep_state->rxq.removed - existing_count;
  if( count_needed > 0 ) {
    struct rte_mbuf *pkts[count_needed];
    if( rte_mempool_get_bulk(m_rings.mempool, (void **) pkts, count_needed) !=
        0 ) {
      ef_log("Unabble to refil the RX fill packets");
      return 0;
    }

    return efswxtch_ef_vi_safe_enqueue(
        m_rings.rx_fill_ring, pkts, count_needed);
  }

  return 1;
}

void efswxtch_vi_init(ef_vi *vi)
{
  efswxtch_init_rings();
  vi->ops.transmit = efswxtch_ef_vi_transmit;
  vi->ops.transmitv = efswxtch_ef_vi_transmitv;
  vi->ops.transmitv_init = efswxtch_ef_vi_transmitv_init;
  vi->ops.transmit_push = efswxtch_ef_vi_transmit_push;
  vi->ops.transmit_pio = efswxtch_ef_vi_transmit_pio;
  vi->ops.transmit_copy_pio = efswxtch_ef_vi_transmit_copy_pio;
  vi->ops.transmit_pio_warm = efswxtch_ef_vi_transmit_pio_warm;
  vi->ops.transmit_copy_pio_warm = efswxtch_ef_vi_transmit_copy_pio_warm;
  vi->ops.transmitv_ctpio = efswxtch_ef_vi_transmitv_ctpio;
  vi->ops.transmitv_ctpio_copy = efswxtch_ef_vi_transmitv_ctpio_copy;
  vi->ops.transmit_alt_select = efswxtch_ef_vi_transmit_alt_select;
  vi->ops.transmit_alt_select_default =
      efswxtch_ef_vi_transmit_alt_select_normal;
  vi->ops.transmit_alt_stop = efswxtch_ef_vi_transmit_alt_stop;
  vi->ops.transmit_alt_go = efswxtch_ef_vi_transmit_alt_go;
  vi->ops.transmit_alt_discard = efswxtch_ef_vi_transmit_alt_discard;
  vi->ops.receive_init = efswxtch_ef_vi_receive_init;
  vi->ops.receive_push = efswxtch_ef_vi_receive_push;
  vi->ops.eventq_poll = efswxtch_ef_eventq_poll;
  vi->ops.eventq_prime = efswxtch_ef_eventq_prime;
  vi->ops.eventq_timer_prime = efswxtch_ef_eventq_timer_prime;
  vi->ops.eventq_timer_run = efswxtch_ef_eventq_timer_run;
  vi->ops.eventq_timer_clear = efswxtch_ef_eventq_timer_clear;
  vi->ops.eventq_timer_zero = efswxtch_ef_eventq_timer_zero;
  vi->ops.transmit_memcpy = efswxtch_ef_vi_transmit_memcpy;
  vi->ops.transmit_memcpy_sync = efswxtch_ef_vi_transmit_memcpy_sync;
  vi->ops.rx_fill_pkt = efswxtch_rx_fill_pkt;
  vi->ops.tx_fill_pkt = efswxtch_ef_vi_tx_fill_pkt;
  vi->ops.refill_rx = efswxtch_ef_vi_refill_rx;
  if( vi->vi_flags & EF_VI_TX_CTPIO ) {
    vi->ops.transmit_ctpio_fallback = efswxtch_ef_vi_transmit_ctpio_fallback;
    vi->ops.transmitv_ctpio_fallback = efswxtch_ef_vi_transmitv_ctpio_fallback;
  } else {
    vi->ops.transmit_ctpio_fallback =
        efswxtch_ef_vi_transmit_ctpio_fallback_not_supp;
    vi->ops.transmitv_ctpio_fallback =
        efswxtch_ef_vi_transmitv_ctpio_fallback_not_supp;
  }

  vi->rx_buffer_len = 2048;
  vi->rx_prefix_len = 0;
  vi->evq_phase_bits = 1; /* We set this flag for ef_eventq_has_event */
  vi->nic_type.arch = EF_VI_ARCH_SWXTCH;
}
