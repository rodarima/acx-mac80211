/* src/idma.c - low level rx and tx management
 *
 * --------------------------------------------------------------------
 *
 * Copyright (C) 2003  ACX100 Open Source Project
 *
 *   The contents of this file are subject to the Mozilla Public
 *   License Version 1.1 (the "License"); you may not use this file
 *   except in compliance with the License. You may obtain a copy of
 *   the License at http://www.mozilla.org/MPL/
 *
 *   Software distributed under the License is distributed on an "AS
 *   IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 *   implied. See the License for the specific language governing
 *   rights and limitations under the License.
 *
 *   Alternatively, the contents of this file may be used under the
 *   terms of the GNU Public License version 2 (the "GPL"), in which
 *   case the provisions of the GPL are applicable instead of the
 *   above.  If you wish to allow the use of your version of this file
 *   only under the terms of the GPL and not to allow others to use
 *   your version of this file under the MPL, indicate your decision
 *   by deleting the provisions above and replace them with the notice
 *   and other provisions required by the GPL.  If you do not delete
 *   the provisions above, a recipient may use your version of this
 *   file under either the MPL or the GPL.
 *
 * --------------------------------------------------------------------
 *
 * Inquiries regarding the ACX100 Open Source Project can be
 * made directly to:
 *
 * acx100-users@lists.sf.net
 * http://acx100.sf.net
 *
 * --------------------------------------------------------------------
 */

#include <linux/config.h>
#include <linux/version.h>

#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/wireless.h>
#if WIRELESS_EXT > 12
#include <net/iw_handler.h>
#endif /* WE > 12 */

#include <wlan_compat.h>

#include <linux/pci.h>



/*================================================================*/
/* Project Includes */

#include <p80211hdr.h>
#include <p80211mgmt.h>
#include <acx100_conv.h>
#include <acx100.h>
#include <p80211types.h>
#include <acx100_helper.h>
#include <acx100_helper2.h>
#include <idma.h>
#include <ihw.h>
#include <monitor.h>

/* these used to be increased to 32 each,
 * but several people had memory allocation issues,
 * so back to 16 again... */
#if (WLAN_HOSTIF==WLAN_USB)
#define TXBUFFERCOUNT 10
#define RXBUFFERCOUNT 10
#else
#define RXBUFFERCOUNT 16
#define TXBUFFERCOUNT 16
#endif

#define MINFREE_TX 3

void acx100_dump_bytes(void *,int);
#if (WLAN_HOSTIF==WLAN_USB)
extern void acx100usb_tx_data(wlandevice_t *,void *);
#endif


/*----------------------------------------------------------------
* acx100_free_desc_queues
*
*	Releases the queues that have been allocated, the
*	others have been initialised to NULL in acx100.c so this
*	function can be used if only part of the queues were
*	allocated.
*
* Arguments:
*
* Returns:
*
* Side effects:
*
* Call context:
*
* STATUS:
*
* Comment:
*
*----------------------------------------------------------------*/
void acx100_free_desc_queues(TIWLAN_DC *pDc)
{
#if (WLAN_HOSTIF!=WLAN_USB)
#define ACX_FREE_QUEUE(size, ptr, phyaddr) \
	if (NULL != ptr) \
	{ \
		pci_free_consistent(0, size, ptr, phyaddr); \
		ptr = NULL; \
		size = 0; \
	}
#else
#define ACX_FREE_QUEUE(size, ptr, phyaddr) \
	if (NULL != ptr) \
	{ \
		kfree(ptr); \
		ptr = NULL; \
		size = 0; \
	}
#endif

	FN_ENTER;

	ACX_FREE_QUEUE(pDc->TxHostDescQPoolSize, pDc->pTxHostDescQPool, pDc->TxHostDescQPoolPhyAddr);
	ACX_FREE_QUEUE(pDc->FrameHdrQPoolSize, pDc->pFrameHdrQPool, pDc->FrameHdrQPoolPhyAddr);
	ACX_FREE_QUEUE(pDc->TxBufferPoolSize, pDc->pTxBufferPool, pDc->TxBufferPoolPhyAddr);
	
	pDc->pTxDescQPool = NULL;
	pDc->tx_pool_count = 0;

	ACX_FREE_QUEUE(pDc->RxHostDescQPoolSize, pDc->pRxHostDescQPool, pDc->RxHostDescQPoolPhyAddr);
	ACX_FREE_QUEUE(pDc->RxBufferPoolSize, pDc->pRxBufferPool, pDc->RxBufferPoolPhyAddr);

	pDc->pRxDescQPool = NULL;
	pDc->rx_pool_count = 0;

	FN_EXIT(0, 0);
}

/*----------------------------------------------------------------
* acx100_dma_tx_data
*
*
* Arguments:
*
* Returns:
*
* Side effects:
*
* Call context:
*
* STATUS:
*
* Comment:
*
*----------------------------------------------------------------*/

/* NB: this table maps RATE111 bits to ACX100 tx descr constants */
static const UINT8 bitpos2rate100[] = {
	RATE100_1		,
	RATE100_2		,
	RATE100_5		,
	0			,
	0			,
	RATE100_11		,
	0			,
	0			,
	RATE100_22		,
};

void acx100_dma_tx_data(wlandevice_t *priv, struct txdescriptor *tx_desc)
{
	struct peer *peer;
	struct txrate_ctrl *txrate;
	struct txhostdescriptor *header;
	struct txhostdescriptor *payload;
#if (WLAN_HOSTIF!=WLAN_USB)
	unsigned long flags;
	int i;
#endif

	FN_ENTER;
	/* header and payload are located in adjacent descriptors */
	header = tx_desc->fixed_size.s.host_desc;
	payload = tx_desc->fixed_size.s.host_desc + 1;

#if (WLAN_HOSTIF!=WLAN_USB)
	/* need to protect access to Ctl field */
	spin_lock_irqsave(&priv->dc.tx_lock, flags);
#endif
	/* DON'T simply set Ctl field to 0 here globally,
	 * it needs to maintain a consistent flag status (those are state flags!!),
	 * otherwise it may lead to severe disruption. Only set or reset particular
	 * flags at the exact moment this is needed...
	 * FIXME: what about Ctl2? Equally problematic? */

	/* uhoh, but acx111 does need a reset here, otherwise it breaks acx111
	 * traffic completely. FIXME: clearing Ctl_8 completely remains wrong,
	 * and it will probably cause the same timing issues as observed
	 * with acx100, so we should find out proper flag management soon
	 * instead of doing such crude hacks */
	if (CHIPTYPE_ACX111 == priv->chip_type) {
		tx_desc->Ctl_8 = 0;
	}

	/* let chip do RTS/CTS handshaking before sending
	 * in case packet size exceeds threshold */
	if (tx_desc->total_length > priv->rts_threshold)
		tx_desc->Ctl2_8 = DESC_CTL2_RTS;
	else
		tx_desc->Ctl2_8 = 0;

	/* TODO: we really need ASSOCIATED_TO_AP/_TO_IBSS */
	if(priv->status == ISTATUS_4_ASSOCIATED
	&& priv->station_assoc.bssid[0] == 0
	)
		peer = &priv->ap_peer;
	else
		peer = &priv->defpeer;

	if (WLAN_GET_FC_FTYPE(((p80211_hdr_t*)header->data)->a3.fc) == WLAN_FTYPE_MGMT)
		txrate = &peer->txbase;
	else
		txrate = &peer->txrate;
 
	tx_desc->fixed_size.s.txc = txrate; /* used in tx cleanup routine for auto rate and accounting */
 
	if (CHIPTYPE_ACX100 == priv->chip_type) {
		/* FIXME: this expensive calculation part should be stored
		 * in advance whenever rate config changes! */

		/* set rate */
		int n = 0;
		UINT16 t = txrate->cur;
		while(t>1) { t>>=1; n++; }
		/* Now n == highest set bit number */
		if(n>=sizeof(bitpos2rate100) || bitpos2rate100[n]==0) {
			printk(KERN_ERR "acx100_dma_tx_data: driver BUG! n=%d. please report\n", n);
			n = 0;
		}

		n = bitpos2rate100[n];
		if(txrate->pbcc511) {
			if(n==RATE100_5 || n==RATE100_11)
				n |= RATE100_PBCC511;
		}
		tx_desc->u.r1.rate = n;
 
		if(peer->shortpre && (txrate->cur != RATE111_1))
			SET_BIT(tx_desc->Ctl_8, ACX100_CTL_PREAMBLE); /* set Short Preamble */

		/* set autodma and reclaim and 1st mpdu */
		SET_BIT(tx_desc->Ctl_8, ACX100_CTL_AUTODMA | ACX100_CTL_RECLAIM | ACX100_CTL_FIRSTFRAG);
	} else { /* ACX111 */
		if (txrate->do_auto)
			tx_desc->u.r2.rate111 = txrate->cur;
		else
		{
			/* need to limit our currently selected rate set
			 * to the highest rate */
			int n = 0;
			UINT16 t = txrate->cur;
			while(t>1) { t>>=1; n++; }
			while(n>0) { t<<=1; n--; }
			/* now we isolated the highest bit */
			tx_desc->u.r2.rate111 = t;
		}
		tx_desc->u.r2.rate111 |=
			/* WARNING: untested. I have no PBCC capable AP --vda */
			(txrate->pbcc511 ? RATE111_PBCC511 : 0)
			/* WARNING: I was never able to make it work with prism54 AP.
			** It was falling down to 1Mbit where shortpre is not applicable,
			** and not working at all at "5,11 basic rates only" setting.
			** I even didn't see tx packets in radio packet capture.
			** Disabled for now --vda */
			//| ((peer->shortpre && txrate->cur!=RATE111_1) ? RATE111_SHORTPRE : 0)
			;

		/* don't need to clean ack/rts statistics here, already
		 * done on descr cleanup */

		if(header->length != sizeof(struct framehdr)) {
			acxlog(L_DATA, "UHOH, packet has a different length than struct framehdr (0x%X vs. 0x%X)\n", sizeof(struct framehdr), header->length );

			/* copy the data at the right place */
			memcpy(header->data + header->length, payload->data, payload->length);
		}
		
		header->length += payload->length;
	}

#if (WLAN_HOSTIF!=WLAN_USB)
	/* sets Ctl ACX100_CTL_OWN to zero telling that the descriptors are now owned by the acx100; do this as LAST operation */
	CLEAR_BIT(header->Ctl_16, cpu_to_le16(ACX100_CTL_OWN));
	CLEAR_BIT(payload->Ctl_16, cpu_to_le16(ACX100_CTL_OWN));
	CLEAR_BIT(tx_desc->Ctl_8, ACX100_CTL_OWN);
	spin_unlock_irqrestore(&priv->dc.tx_lock, flags);

	tx_desc->tx_time = cpu_to_le32(jiffies);
	acx100_write_reg16(priv, priv->io[IO_ACX_INT_TRIG], 0x4);
#else
	tx_desc->tx_time = cpu_to_le32(jiffies);
	acx100usb_tx_data(priv, tx_desc);
#endif
	/* log the packet content AFTER sending it,
	 * in order to not delay sending any further than absolutely needed
	 * Do separate logs for acx100/111 to have human-readable rates */
	if (CHIPTYPE_ACX100 == priv->chip_type)
		acxlog(L_XFER | L_DATA,
			"tx: pkt (%s): len %i (%i/%i) mode %d rate %03d status %d\n",
			acx100_get_packet_type_string(((p80211_hdr_t*)header->data)->a3.fc),
			tx_desc->total_length,
			header->length,
			payload->length,
			priv->macmode_joined,
			tx_desc->u.r1.rate,
			priv->status);
	else
		acxlog(L_XFER | L_DATA,
			"tx: pkt (%s): len %i (%i/%i) mode %d rate %04x status %d\n",
			acx100_get_packet_type_string(((p80211_hdr_t*)header->data)->a3.fc),
			tx_desc->total_length,
			header->length,
			payload->length,
			priv->macmode_joined,
			tx_desc->u.r2.rate111,
			priv->status);

	if (debug & L_DATA)
	{
		acxlog(L_DATA, "tx: 802.11 header[%d]: ", header->length);
#if (WLAN_HOSTIF==WLAN_USB)
		acx100_dump_bytes(header->data, header->length);
#else
		for (i = 0; i < header->length; i++)
			printk("%02x ", ((UINT8 *) header->data)[i]);
		printk("\n");
#endif
		acxlog(L_DATA, "tx: 802.11 payload[%d]: ", payload->length);
#if (WLAN_HOSTIF==WLAN_USB)
		acx100_dump_bytes(payload->data, payload->length);
#else
		for (i = 0; i < payload->length; i++)
			printk("%02x ", ((UINT8 *) payload->data)[i]);
		printk("\n");
#endif
	}
	FN_EXIT(0, OK);
}

static void acx_handle_tx_error(wlandevice_t *priv, txdesc_t *pTxDesc)
{
	char *err = "unknown error";

	/* hmm, should we handle this as a mask
	 * of *several* bits?
	 * For now I think only caring about
	 * individual bits is ok... */
	switch(pTxDesc->error) {
		case 0x01:
			err = "no Tx due to error in other fragment";
			priv->wstats.discard.fragment++;
			break;
		case 0x02:
			err = "Tx aborted";
			priv->stats.tx_aborted_errors++;
			break;
		case 0x04:
			err = "Tx desc wrong parameters";
			priv->wstats.discard.misc++;
			break;
		case 0x08:
			err = "WEP key not found";
			priv->wstats.discard.misc++;
			break;
		case 0x10:
			err = "MSDU lifetime timeout? - change 'iwconfig retry lifetime XXX'";
			priv->wstats.discard.misc++;
			break;
		case 0x20:
			err = "excessive Tx retries due to either distance too high "
			"or unable to Tx or Tx frame error - change "
			"'iwconfig txpower XXX' or 'sens'itivity or 'retry'";
			priv->wstats.discard.retries++;
			/* FIXME: set (GETSET_TX|GETSET_RX) here
			 * (this seems to recalib radio on ACX100)
			 * after some more jiffies passed??
			 * But OTOH Tx error 0x20 also seems to occur on
			 * overheating, so I'm not sure whether we
			 * actually want that, since people maybe won't notice
			 * then that their hardware is slowly getting
			 * cooked...
			 * Or is it still a safe long distance from utter
			 * radio non-functionality despite many radio
			 * recalibs
			 * to final destructive overheating of the hardware?
			 * In this case we really should do recalib here...
			 * I guess the only way to find out is to do a
			 * potentially fatal self-experiment :-\
			 * Or maybe only recalib in case we're using Tx
			 * rate auto (on errors switching to lower speed
			 * --> less heat?) or 802.11 power save mode? */
			break;
		case 0x40:
			err = "Tx buffer overflow";
			priv->stats.tx_fifo_errors++;
			break;
		case 0x80:
			err = "DMA error";
			priv->wstats.discard.misc++;
			break;
	}
	acxlog(L_STD, "tx: error occurred (0x%02X)!! (%s)\n", pTxDesc->error, err);
	priv->stats.tx_errors++;

#if (WLAN_HOSTIF!=WLAN_USB)
	/* TODO critical!! is this block valid? */
	/* let card know there is work to do */
	acx100_write_reg16(priv, priv->io[IO_ACX_INT_TRIG], 0x4);
#endif

#if WIRELESS_EXT > 13 /* wireless_send_event() and IWEVTXDROP are WE13 */
	if (0x30 & pTxDesc->error) {
		/* only send IWEVTXDROP in case of retry or lifetime exceeded;
		 * all other errors mean we screwed up locally */
		union iwreq_data wrqu;
		p80211_hdr_t *hdr = (p80211_hdr_t *)pTxDesc->fixed_size.s.host_desc->data;

		MAC_COPY(wrqu.addr.sa_data, hdr->a3.a1);
		wireless_send_event(priv->netdev, IWEVTXDROP, &wrqu, NULL);
	}
#endif
}

/* Theory of operation:
priv->txrate.cfg is a bitmask of allowed (configured) rates.
It is set as a result of iwconfig rate N [auto]
or iwpriv set_rates "N,N,N N,N,N" commands.
It can be fixed (e.g. 0x0080 == 18Mbit only),
auto (0x00ff == 18Mbit or any lower value),
and code handles any bitmask (0x1081 == try 54Mbit,18Mbit,1Mbit _only_).

priv->txrate.cur is a value for rate111 field in tx descriptor.
It is always set to txrate_cfg sans zero or more most significant
bits. This routine handles selection of txrate_curr depending on
outcome of last tx event.

You cannot configure mixed usage of 5.5 and/or 11Mbit rate
with PBCC and CCK modulation. Either both at CCK or both at PBCC.
In theory you can implement it, but so far it is considered not worth doing.

22Mbit, of course, is PBCC always.
*/

/* maps acx100 tx descr rate field to acx111 one */
static UINT16
rate100to111(UINT8 r)
{
	switch(r) {
	case RATE100_1:	return RATE111_1;
	case RATE100_2:	return RATE111_2;
	case RATE100_5:
	case RATE100_5 | RATE100_PBCC511:	return RATE111_5;
	case RATE100_11:
	case RATE100_11 | RATE100_PBCC511:	return RATE111_11;
	case RATE100_22:	return RATE111_22;
	default:
		printk(KERN_DEBUG "Unexpected acx100 txrate of %d! Please report\n",r);
		return RATE111_2;
	}
}

static void
do_handle_txrate_auto(struct txrate_ctrl *txrate, UINT16 rate, UINT8 error)
{
	UINT16 cur = txrate->cur;
	int slower_rate_was_used;

	acxlog(L_DEBUG, "tx: rate mask %04x/%04x/%04x, fallback %d/%d, stepup %d/%d\n",
		rate, cur, txrate->cfg,
		txrate->fallback_count, txrate->fallback_threshold,
		txrate->stepup_count, txrate->stepup_threshold
	);

	/* 
	cur < rate: old tx packet, before tx_curr went in effect
	else cur >= rate and:
	(cur^rate) >= rate: true only if highest set bit
	in cur is more significant than highest set bit in rate
	*/
	slower_rate_was_used = (cur > rate) && ((cur ^ rate) >= rate);
	
	if (slower_rate_was_used || (0 != (error & 0x30))) {
		txrate->stepup_count = 0;
		if (++txrate->fallback_count <= txrate->fallback_threshold) 
			return;
		txrate->fallback_count = 0;

		/* clear highest 1 bit in cur */
		rate=0x1000;
		while(0 == (cur & rate))
			rate >>= 1;
		CLEAR_BIT(cur, rate);

		if(cur) { /* we can't disable all rates! */
			acxlog(L_XFER, "tx: falling back to rate mask %04x\n", cur);
			txrate->cur = cur;
		}
	}
	else
	if (!slower_rate_was_used) {
		txrate->fallback_count = 0;
		if (++txrate->stepup_count <= txrate->stepup_threshold)
			return;
		txrate->stepup_count = 0;
		
		/* try to find higher allowed rate */
		do
		    rate<<=1;
		while( rate && ((txrate->cfg & rate) == 0));
		
		if (!rate) 
		    return; /* no higher rates allowed by config */
		
		SET_BIT(cur, rate);
		acxlog(L_XFER, "tx: stepping up to rate mask %04x\n", cur);
		txrate->cur = cur;
	}
}

static void
acx_handle_txrate_auto(wlandevice_t *priv, struct txrate_ctrl *txc, txdesc_t *pTxDesc)
{
	UINT16 rate;
 
	if(CHIPTYPE_ACX100 == priv->chip_type) {
		rate = rate100to111(pTxDesc->u.r1.rate);
	 } else {
		int n = 0;
		rate = pTxDesc->u.r2.rate111 & 0x1fff;
		while(rate>1) { rate>>=1; n++; }
		rate = 1<<n;
	}
	/* rate has only one bit set now, corresponding to tx rate 
	** which was used by hardware to tx this particular packet */
 
	do_handle_txrate_auto(txc, rate, pTxDesc->error);
}
 
/*----------------------------------------------------------------
* acx100_log_txbuffer
*
*
* Arguments:
*
* Returns:
*
* Side effects:
*
* Call context:
*
* STATUS:
*
* Comment:
*
*----------------------------------------------------------------*/

static inline void acx100_log_txbuffer(TIWLAN_DC *pDc)
{
	unsigned int i;
	txdesc_t *pTxDesc;

	FN_ENTER;
	if (debug & L_DEBUG) /* we don't want this to be in L_BUFT, too */
	{
		/* no locks here, since it's entirely non-critical code */
		pTxDesc = pDc->pTxDescQPool;
		for (i = 0; i < pDc->tx_pool_count; i++)
		{
			if ((pTxDesc->Ctl_8 & DESC_CTL_DONE) == DESC_CTL_DONE)
				acxlog(L_DEBUG, "tx: buf %d done\n", i);
			pTxDesc = GET_NEXT_TX_DESC_PTR(pDc, pTxDesc);
		}
	}
	FN_EXIT(0, OK);
}

/*----------------------------------------------------------------
* acx100_clean_tx_desc
*
*
* Arguments:
*
* Returns:
*
* Side effects:
*
* Call context:
*
* STATUS:
*
* Comment:
*	This function probably resets the txdescs' status when the ACX100
*	signals the TX done IRQ (txdescs have been processed), starting with
*	the pool index of the descriptor which we would use next,
*	in order to make sure that we can be as fast as possible
*	in filling new txdescs.
*	Oops, now we have our own index, so everytime we get called we know
*	where the next packet to be cleaned is.
*	Hmm, still need to loop through the whole ring buffer now,
*	since we lost sync for some reason when ping flooding or so...
*	(somehow we don't get the IRQ for acx100_clean_tx_desc any more when
*	too many packets are being sent!)
*	FIXME: currently we only process one packet, but this gets out of
*	sync for some reason when ping flooding, so we need to loop,
*	but the previous smart loop implementation causes the ping latency
*	to rise dramatically (~3000 ms), at least on CardBus PheeNet WL-0022.
*	Dunno what to do :-\
*
*----------------------------------------------------------------*/

inline void acx100_clean_tx_desc(wlandevice_t *priv)
{
	TIWLAN_DC *pDc = &priv->dc;
	txdesc_t *pTxDesc;
	UINT finger, watch;
	UINT8 ack_failures, rts_failures, rts_ok; /* keep old desc status */
	UINT16 r111;
	UINT16 num_cleaned = 0;
	UINT16 num_processed = 0;

	FN_ENTER;

	acx100_log_txbuffer(pDc);
	acxlog(L_BUFT, "tx: cleaning up bufs from %d\n", pDc->tx_tail);

	spin_lock(&pDc->tx_lock);

	finger = pDc->tx_tail;
	watch = finger;

	do {
		pTxDesc = GET_TX_DESC_PTR(pDc, finger);

		/* abort if txdesc is not marked as "Tx finished" and "owned" */
		if ((pTxDesc->Ctl_8 & DESC_CTL_DONE) != DESC_CTL_DONE)
		{
			/* we do need to have at least one cleaned,
			 * otherwise we wouldn't get called in the first place.
			 * So better stay around some more, unless
			 * we already processed more descs than the ring
			 * size. */
			if ((num_cleaned == 0) && (num_processed < pDc->tx_pool_count))
				goto next;
			else
				break;
		}

		/* ok, need to clean / handle this descriptor */
		if (unlikely(0 != pTxDesc->error))
			acx_handle_tx_error(priv, pTxDesc);

                {
			struct txrate_ctrl *txc = pTxDesc->fixed_size.s.txc;
			if (txc != &priv->defpeer.txbase
			&&  txc != &priv->defpeer.txrate
			&&  txc != &priv->ap_peer.txbase
			&&  txc != &priv->ap_peer.txrate
			) {
				printk(KERN_WARNING "Probable BUG in acx100 driver: txdescr->txc %08x is bad!\n", (u32)txc);
			} else if (txc->do_auto) {
				acx_handle_txrate_auto(priv, txc, pTxDesc);
			}
 
		}

		ack_failures = pTxDesc->ack_failures;
		rts_failures = pTxDesc->rts_failures;
		rts_ok = pTxDesc->rts_ok;
		r111 = pTxDesc->u.r2.rate111;

		/* free it */
		pTxDesc->ack_failures = 0;
		pTxDesc->rts_failures = 0;
		pTxDesc->rts_ok = 0;
		pTxDesc->error = 0;
		pTxDesc->Ctl_8 = ACX100_CTL_OWN;
		priv->TxQueueFree++;
		num_cleaned++;

		if ((priv->TxQueueFree >= MINFREE_TX + 3)
		&& (priv->status == ISTATUS_4_ASSOCIATED)
		&& (netif_queue_stopped(priv->netdev)))
		{
			/* FIXME: if construct is ugly:
			 * should have functions acx100_stop_queue
			 * etc. which set flag priv->tx_stopped
			 * to be checked here. */
			acxlog(L_BUF, "tx: wake queue (avail. Tx desc %d)\n", priv->TxQueueFree);
			netif_wake_queue(priv->netdev);
		}
		/* log AFTER having done the work, faster */
		acxlog(L_BUFT, "tx: cleaned %d: ack_fail=%d rts_fail=%d rts_ok=%d r111=%04x\n",
			finger, ack_failures, rts_failures, rts_ok, r111);

next:
		/* update pointer for descr to be cleaned next */
		finger = (finger + 1) % pDc->tx_pool_count;
		num_processed++;
	} while (watch != finger);

	/* remember last position */
	pDc->tx_tail = finger;

	spin_unlock(&pDc->tx_lock);

	FN_EXIT(0, OK);
	return;
}
/*----------------------------------------------------------------
* acx100_rxmonitor
*
*
* Arguments:
*
* Returns:
*
* Side effects:
*
* Call context:
*
* STATUS:
*
* Comment:
*
*----------------------------------------------------------------*/

static void acx100_rxmonitor(wlandevice_t *priv, struct rxbuffer *buf)
{
	unsigned long *data = (unsigned long *)buf;
	unsigned int packet_len = data[0] & 0xFFF;
	int sig_strength = (data[1] >> 16) & 0xFF;
	int sig_quality  = (data[1] >> 24) & 0xFF;
	p80211msg_lnxind_wlansniffrm_t *msg;

	int payload_offset = 0;
	unsigned int skb_len;
	struct sk_buff *skb;
	void *datap;

	FN_ENTER;

	if (!(priv->rx_config_1 & RX_CFG1_PLUS_ADDIT_HDR))
	{
		printk("rx_config_1 misses RX_CFG1_PLUS_ADDIT_HDR\n");
		FN_EXIT(0, 0);
		return;
	}

	if (priv->rx_config_1 & RX_CFG1_PLUS_ADDIT_HDR)
	{
		payload_offset += 3*4; /* status words         */
		packet_len     += 3*4; /* length is w/o status */
	}
		
	if (priv->rx_config_1 & RX_CFG1_INCLUDE_ADDIT_HDR)
		payload_offset += 4;   /* phy header   */

	/* we are in big luck: the acx100 doesn't modify any of the fields */
	/* in the 802.11-frame. just pass this packet into the PF_PACKET-  */
	/* subsystem. yeah. */

	skb_len = packet_len - payload_offset;

	if (priv->netdev->type == ARPHRD_IEEE80211_PRISM)
		skb_len += sizeof(p80211msg_lnxind_wlansniffrm_t);

		/* sanity check */
	if (skb_len > (WLAN_HDR_A4_LEN + WLAN_DATA_MAXLEN + WLAN_CRC_LEN))
	{
		printk("monitor mode panic: oversized frame!\n");
		FN_EXIT(0, NOT_OK);
		return;
	}

		/* allocate skb */
	if ( (skb = dev_alloc_skb(skb_len)) == NULL)
	{
		printk("alloc_skb failed trying to allocate %d bytes\n", skb_len);
		FN_EXIT(0, NOT_OK);
		return;
	}

	skb_put(skb, skb_len);

		/* when in raw 802.11 mode, just copy frame as-is */
	if (priv->netdev->type == ARPHRD_IEEE80211)
		datap = skb->data;
	else  /* otherwise, emulate prism header */
	{
		msg = (p80211msg_lnxind_wlansniffrm_t*)skb->data;
		datap = msg + 1;
		
		msg->msgcode = DIDmsg_lnxind_wlansniffrm;
		msg->msglen = sizeof(p80211msg_lnxind_wlansniffrm_t);
		strcpy(msg->devname, priv->netdev->name);
		
		msg->hosttime.did = DIDmsg_lnxind_wlansniffrm_hosttime;
		msg->hosttime.status = 0;
		msg->hosttime.len = 4;
		msg->hosttime.data = jiffies;
		
		msg->mactime.did = DIDmsg_lnxind_wlansniffrm_mactime;
		msg->mactime.status = 0;
		msg->mactime.len = 4;
		msg->mactime.data = data[2];
		
		msg->channel.did = DIDmsg_lnxind_wlansniffrm_channel;
		msg->channel.status = P80211ENUM_msgitem_status_no_value;
		msg->channel.len = 4;
		msg->channel.data = 0;

		msg->rssi.did = DIDmsg_lnxind_wlansniffrm_rssi;
		msg->rssi.status = P80211ENUM_msgitem_status_no_value;
		msg->rssi.len = 4;
		msg->rssi.data = 0;

		msg->sq.did = DIDmsg_lnxind_wlansniffrm_sq;
		msg->sq.status = P80211ENUM_msgitem_status_no_value;
		msg->sq.len = 4;
		msg->sq.data = 0;

		msg->signal.did = DIDmsg_lnxind_wlansniffrm_signal;
		msg->signal.status = 0;
		msg->signal.len = 4;
		msg->signal.data = sig_quality;

		msg->noise.did = DIDmsg_lnxind_wlansniffrm_noise;
		msg->noise.status = 0;
		msg->noise.len = 4;
		msg->noise.data = sig_strength;

		msg->rate.did = DIDmsg_lnxind_wlansniffrm_rate;
		msg->rate.status = P80211ENUM_msgitem_status_no_value;
		msg->rate.len = 4;
		msg->rate.data = 0;

		msg->istx.did = DIDmsg_lnxind_wlansniffrm_istx;
		msg->istx.status = 0;
		msg->istx.len = 4;
		msg->istx.data = P80211ENUM_truth_false;

		skb_len -= sizeof(p80211msg_lnxind_wlansniffrm_t);

		msg->frmlen.did = DIDmsg_lnxind_wlansniffrm_signal;
		msg->frmlen.status = 0;
		msg->frmlen.len = 4;
		msg->frmlen.data = skb_len;
	}

	memcpy(datap, ((unsigned char*)buf)+payload_offset, skb_len);

	skb->dev = priv->netdev;
	skb->dev->last_rx = jiffies;

	skb->mac.raw = skb->data;
	skb->ip_summed = CHECKSUM_NONE;
	skb->pkt_type = PACKET_OTHERHOST;
	skb->protocol = htons(ETH_P_80211_RAW);
	priv->stats.rx_packets++;
	priv->stats.rx_bytes += skb->len;

	netif_rx(skb);
	FN_EXIT(0, OK);
}

/*
 * Calculate level like the feb 2003 windows driver seems to do
 */
UINT8 acx_signal_to_winlevel(UINT8 rawlevel)
{
	/* UINT8 winlevel = (UINT8) (0.5 + 0.625 * rawlevel); */
	UINT8 winlevel = (UINT8) ((4 + (rawlevel * 5)) / 8);

	if(winlevel>100)
		winlevel=100;

	return winlevel;
}

UINT8 acx_signal_determine_quality(UINT8 signal, UINT8 noise)
{
	int qual;

	qual = (((signal - 30) * 100 / 70) + (100 - noise * 4)) / 2;

	if (qual > 100)
		return 100;
	if (qual < 0)
		return 0;
	return (UINT8)qual;
}

/*----------------------------------------------------------------
* acx100_log_rxbuffer
*
*
* Arguments:
*
* Returns:
*
* Side effects:
*
* Call context:
*
* STATUS:
*
* Comment:
*
*----------------------------------------------------------------*/

static inline void acx100_log_rxbuffer(TIWLAN_DC *pDc)
{
	unsigned int i;
	struct rxhostdescriptor *pRxDesc;

	FN_ENTER;
	if (debug & L_BUFR)
	{
		/* no locks here, since it's entirely non-critical code */
		pRxDesc = pDc->pRxHostDescQPool;
		for (i = 0; i < pDc->rx_pool_count; i++)
		{
#if (WLAN_HOSTIF==WLAN_USB)
			acxlog(L_DEBUG,"rx: buf %d Ctl=%X val0x14=%X\n",i,le16_to_cpu(pRxDesc->Ctl_16),pRxDesc->Status);
#endif
			if ((le16_to_cpu(pRxDesc->Ctl_16) & ACX100_CTL_OWN) && (le32_to_cpu(pRxDesc->Status) & BIT31))
				acxlog(L_BUFR, "rx: buf %d full\n", i);
			pRxDesc++;
		}
	}
	FN_EXIT(0, 0);
}

/* currently we don't need to lock anything, since we access Rx
 * descriptors from one IRQ handler only (which is locked), here.
 * Will need to use spin_lock() when adding user context Rx descriptor
 * access. */
#define RX_SPIN_LOCK(lock)
#define RX_SPIN_UNLOCK(lock)

/*------------------------------------------------------------------------------
 * acx100_process_rx_desc
 *
 * RX path top level entry point called directly and only from the IRQ handler
 *
 * Arguments:
 *
 * Returns:
 *
 * Side effects:
 *
 * Call context: Hard IRQ
 *
 * STATUS:
 *
 * Comment:
 *
 *----------------------------------------------------------------------------*/
inline void acx100_process_rx_desc(wlandevice_t *priv)
{
	struct rxhostdescriptor *RxHostPool;
	TIWLAN_DC *pDc;
	struct rxhostdescriptor *pRxHostDesc;
	UINT16 buf_len;
	int curr_idx;
	unsigned int count = 0;
	p80211_hdr_t *buf;
	int qual;

	FN_ENTER;

	pDc = &priv->dc;
	acx100_log_rxbuffer(pDc);

	/* there used to be entry count code here, but because this function is called
	 * by the interrupt handler, we are sure that this will only be entered once
	 * because the kernel locks the interrupt handler */

	RxHostPool = pDc->pRxHostDescQPool;

	/* First, have a loop to determine the first descriptor that's
	 * full, just in case there's a mismatch between our current
	 * rx_tail and the full descriptor we're supposed to handle. */
	while (1) {
		/* we're not in user context but in IRQ context here,
		 * so we don't need to use _irqsave() since an IRQ can
		 * only happen once anyway. user context access DOES
		 * need to prevent US from having an IRQ, however
		 * (_irqsave) */
		RX_SPIN_LOCK(&pDc->rx_lock);
		curr_idx = pDc->rx_tail;
		pRxHostDesc = &RxHostPool[pDc->rx_tail];
		pDc->rx_tail = (pDc->rx_tail + 1) % pDc->rx_pool_count;
		if ((le16_to_cpu(pRxHostDesc->Ctl_16) & ACX100_CTL_OWN) && (le32_to_cpu(pRxHostDesc->Status) & BIT31)) {
			RX_SPIN_UNLOCK(&pDc->rx_lock);
			break;
		}
		RX_SPIN_UNLOCK(&pDc->rx_lock);
		count++;
		if (unlikely(count > pDc->rx_pool_count))
		{ /* hmm, no luck: all descriptors empty, bail out */
			FN_EXIT(0, 0);
			return;
		}
	}

	/* now process descriptors, starting with the first we figured out */
	while (1)
	{
		acxlog(L_BUFR, "%s: using curr_idx %d, rx_tail is now %d\n", __func__, curr_idx, pDc->rx_tail);

		/* enable for a sweet dump of the frame */
		/* acx100_dump_bytes(buf,buf_len); */

		if (priv->rx_config_1 & RX_CFG1_INCLUDE_ADDIT_HDR) {
			/* take into account additional header in front of packet */
			if(priv->chip_type == CHIPTYPE_ACX111) {
				buf = (p80211_hdr_t*)((UINT8*)&pRxHostDesc->data->buf + 8);
			} else {
				buf = (p80211_hdr_t*)((UINT8*)&pRxHostDesc->data->buf + 4);
			}

		}
		else
		{
			buf = (p80211_hdr_t *)&pRxHostDesc->data->buf;
		}

		buf_len = le16_to_cpu(pRxHostDesc->data->mac_cnt_rcvd) & 0xfff;      /* somelength */
		if ((WLAN_GET_FC_FSTYPE(le16_to_cpu(buf->a3.fc)) != WLAN_FSTYPE_BEACON)
		||  (debug & L_XFER_BEACON))
			acxlog(L_XFER|L_DATA, "rx: pkt %02d (%s): "
				"time %u len %i signal %d SNR %d macstat %02x phystat %02x phyrate %u mode %d status %d\n",
				curr_idx,
				acx100_get_packet_type_string(le16_to_cpu(buf->a3.fc)),
				le32_to_cpu(pRxHostDesc->data->time),
				buf_len,
				acx_signal_to_winlevel(pRxHostDesc->data->phy_level),
				acx_signal_to_winlevel(pRxHostDesc->data->phy_snr),
				pRxHostDesc->data->mac_status,
				pRxHostDesc->data->phy_stat_baseband,
				pRxHostDesc->data->phy_plcp_signal,
				priv->macmode_joined,
				priv->status);

		/* FIXME: should check for Rx errors (pRxHostDesc->data->mac_status?
		 * discard broken packets - but NOT for monitor!)
		 * and update Rx packet statistics here */

		if (unlikely(priv->monitor)) {
			acx100_rxmonitor(priv, pRxHostDesc->data);
		} else if (buf_len >= 14) {
			acx100_rx_ieee802_11_frame(priv, pRxHostDesc);
		} else {
			acxlog(L_DEBUG | L_XFER | L_DATA,
			       "rx: NOT receiving packet (%s): size too small (%d)\n",
			       acx100_get_packet_type_string(le16_to_cpu(buf->a3.fc)), buf_len);
		}

		/* Now check Rx quality level, AFTER processing packet.
		 * I tried to figure out how to map these levels to dBm
		 * values, but for the life of me I really didn't
		 * manage to get it. Either these values are not meant to
		 * be expressed in dBm, or it's some pretty complicated
		 * calculation. */

#if FROM_SCAN_SOURCE_ONLY
		/* only consider packets originating from the MAC
		 * address of the device that's managing our BSSID.
		 * Disable it for now, since it removes information (levels
		 * from different peers) and slows the Rx path. */
		if (0 == memcmp(buf->a3.a2, priv->station_assoc.mac_addr, ETH_ALEN)) {
#endif
			priv->wstats.qual.level = acx_signal_to_winlevel(pRxHostDesc->data->phy_level);
			priv->wstats.qual.noise = acx_signal_to_winlevel(pRxHostDesc->data->phy_snr);
#ifndef OLD_QUALITY
			qual = acx_signal_determine_quality(priv->wstats.qual.level, priv->wstats.qual.noise);
#else
			qual = (priv->wstats.qual.noise <= 100) ?
					100 - priv->wstats.qual.noise : 0;
#endif
			priv->wstats.qual.qual = qual;
			priv->wstats.qual.updated = 7; /* all 3 indicators updated */
#if FROM_SCAN_SOURCE_ONLY
		}
#endif

		RX_SPIN_LOCK(&pDc->rx_lock);
		CLEAR_BIT(pRxHostDesc->Ctl_16, cpu_to_le16(ACX100_CTL_OWN)); /* Host no longer owns this */
		pRxHostDesc->Status = 0;

		/* ok, descriptor is handled, now check the next descriptor */
		curr_idx = pDc->rx_tail;
		pRxHostDesc = &RxHostPool[pDc->rx_tail];

		/* if next descriptor is empty, then bail out */
		/* if (!((le16_to_cpu(pRxHostDesc->Ctl) & ACX100_CTL_OWN) && (!(le32_to_cpu(pRxHostDesc->Status) & BIT31)))) */
		if (!(le32_to_cpu(pRxHostDesc->Status) & BIT31))
		{
			RX_SPIN_UNLOCK(&pDc->rx_lock);
			break;
		}
		else
			pDc->rx_tail = (pDc->rx_tail + 1) % pDc->rx_pool_count;
		RX_SPIN_UNLOCK(&pDc->rx_lock);
	}
	FN_EXIT(0, 0);
}

/*----------------------------------------------------------------
* acx100_create_tx_host_desc_queue
*
*
* Arguments:
*
* Returns:
*
* Side effects:
*
* Call context:
*
* STATUS:
*
* Comment:
*
*----------------------------------------------------------------*/
static int acx100_create_tx_host_desc_queue(TIWLAN_DC *pDc)
{
	wlandevice_t *priv;
	int res = 2;

	UINT i;
#if (WLAN_HOSTIF!=WLAN_USB)
	UINT align_offs;
	UINT alignment;

	struct framehdr *frame_hdr_phy;
	UINT8 *frame_payload_phy;
	struct txhostdescriptor *host_desc_phy;
#endif

	struct framehdr *frame_hdr;
	UINT8 *frame_payload;

	struct txhostdescriptor *host_desc;

	FN_ENTER;

	priv = pDc->priv;

	/* allocate TX header pool */
	pDc->FrameHdrQPoolSize = (priv->TxQueueCnt * sizeof(struct framehdr));

#if (WLAN_HOSTIF!=WLAN_USB)
	if (!(pDc->pFrameHdrQPool =
	      pci_alloc_consistent(0, pDc->FrameHdrQPoolSize, &pDc->FrameHdrQPoolPhyAddr))) {
		acxlog(L_BINSTD, "pDc->pFrameHdrQPool memory allocation fail\n");
		goto fail;
	}
	acxlog(L_BINDEBUG, "pDc->pFrameHdrQPool = 0x%08x\n", (UINT) pDc->pFrameHdrQPool);
	acxlog(L_BINDEBUG, "pDc->pFrameHdrQPoolPhyAddr = 0x%08x\n", (UINT) pDc->FrameHdrQPoolPhyAddr);
#else
	if ((pDc->pFrameHdrQPool=kmalloc(pDc->FrameHdrQPoolSize,GFP_KERNEL))==NULL) {
		acxlog(L_STD,"pDc->pFrameHdrQPool memory allocation fail\n");
		goto fail;
	}
	memset(pDc->pFrameHdrQPool,0,pDc->FrameHdrQPoolSize);
#endif

	/* allocate TX payload pool */
	pDc->TxBufferPoolSize = priv->TxQueueCnt*2 * (WLAN_MAX_ETHFRM_LEN - WLAN_ETHHDR_LEN);
#if (WLAN_HOSTIF!=WLAN_USB)
	if (!(pDc->pTxBufferPool =
	      pci_alloc_consistent(0, pDc->TxBufferPoolSize, &pDc->TxBufferPoolPhyAddr))) {
		acxlog(L_BINSTD, "pDc->pTxBufferPool memory allocation fail\n");
		goto fail;
	}
	acxlog(L_BINDEBUG, "pDc->TxBufferPoolSize = 0x%08x\n", (UINT) pDc->TxBufferPoolSize);
	acxlog(L_BINDEBUG, "pDc->TxBufferPool = 0x%08x\n", (UINT) pDc->pTxBufferPool);
	acxlog(L_BINDEBUG, "pDc->TxBufferPoolPhyAddr = 0x%08x\n", (UINT) pDc->TxBufferPoolPhyAddr);
#else
	if ((pDc->pTxBufferPool=kmalloc(pDc->TxBufferPoolSize,GFP_KERNEL))==NULL) {
		acxlog(L_STD,"pDc->pTxBufferPool memory allocation fail\n");
		goto fail;
	}
	memset(pDc->pTxBufferPool,0,pDc->TxBufferPoolSize);
#endif

	/* allocate the TX host descriptor queue pool */
	pDc->TxHostDescQPoolSize =  priv->TxQueueCnt*2 * sizeof(struct txhostdescriptor) + 3;
#if (WLAN_HOSTIF!=WLAN_USB)
	if (!(pDc->pTxHostDescQPool =
	      pci_alloc_consistent(0, pDc->TxHostDescQPoolSize,
				  &pDc->TxHostDescQPoolPhyAddr))) {
		acxlog(L_BINSTD, "Failed to allocate shared memory for TxHostDesc queue\n");
		goto fail;
	}
	acxlog(L_BINDEBUG, "pDc->pTxHostDescQPool = 0x%08x\n", (UINT) pDc->pTxHostDescQPool);
	acxlog(L_BINDEBUG, "pDc->TxHostDescQPoolPhyAddr = 0x%08x\n", pDc->TxHostDescQPoolPhyAddr);
#else
	if ((pDc->pTxHostDescQPool=kmalloc(pDc->TxHostDescQPoolSize,GFP_KERNEL))==NULL) {
		acxlog(L_STD,"Failed to allocate memory for TxHostDesc queue\n");
		goto fail;
	}
	memset(pDc->pTxHostDescQPool,0,pDc->TxHostDescQPoolSize);
#endif

#if (WLAN_HOSTIF!=WLAN_USB)
	/* check for proper alignment of TX host descriptor pool */
	alignment = (UINT) pDc->pTxHostDescQPool & 3;
	if (alignment) {
		acxlog(L_BINSTD, "%s: TxHostDescQPool not aligned properly\n", __func__);
		align_offs = 4 - alignment;
	} else {
		align_offs = 0;
	}

	host_desc = (struct txhostdescriptor *) ((UINT8 *) pDc->pTxHostDescQPool + align_offs);
	host_desc_phy = (struct txhostdescriptor *) ((UINT8 *) pDc->TxHostDescQPoolPhyAddr + align_offs);
	frame_hdr_phy = (struct framehdr *) pDc->FrameHdrQPoolPhyAddr;
	frame_payload_phy = (UINT8 *) pDc->TxBufferPoolPhyAddr;
#else
	host_desc = (struct txhostdescriptor *)pDc->pTxHostDescQPool;
#endif
	frame_hdr = (struct framehdr *) pDc->pFrameHdrQPool;
	frame_payload = (UINT8 *) pDc->pTxBufferPool;

	for (i = 0; i < priv->TxQueueCnt*2 - 1; i++)
	{
		if (!(i & 1)) {
#if (WLAN_HOSTIF!=WLAN_USB)
			host_desc->data_phy = (ACX_PTR)cpu_to_le32(frame_hdr_phy);
			frame_hdr_phy++;
			host_desc->pNext = (ACX_PTR)cpu_to_le32(((UINT8 *) host_desc_phy + sizeof(struct txhostdescriptor)));
#endif
			host_desc->data = (UINT8 *)frame_hdr;
			frame_hdr++;
		} else {
#if (WLAN_HOSTIF!=WLAN_USB)
			host_desc->data_phy = (ACX_PTR)cpu_to_le32(frame_payload_phy);
			frame_payload_phy += WLAN_MAX_ETHFRM_LEN - WLAN_ETHHDR_LEN;
#endif
			host_desc->data = frame_payload;
			frame_payload += WLAN_MAX_ETHFRM_LEN - WLAN_ETHHDR_LEN;
			host_desc->pNext = (ACX_PTR)NULL;
		}

		host_desc->Ctl_16 = cpu_to_le16(ACX100_CTL_OWN);
#if (WLAN_HOSTIF!=WLAN_USB)
		host_desc->desc_phy = host_desc_phy;
		host_desc->desc_phy_next = (ACX_PTR)cpu_to_le32(((UINT8 *) host_desc_phy + sizeof(struct txhostdescriptor)));
		host_desc_phy++;
#endif
		host_desc++;
	}
	host_desc->data = (UINT8 *)cpu_to_le32(frame_payload);
	host_desc->pNext = 0;
	host_desc->Ctl_16 = cpu_to_le16(ACX100_CTL_OWN);

#if (WLAN_HOSTIF!=WLAN_USB)
	host_desc->data_phy = (ACX_PTR)cpu_to_le32(frame_payload_phy);
	host_desc->desc_phy = host_desc_phy;
	host_desc->desc_phy_next = (ACX_PTR)cpu_to_le32(((UINT8 *) pDc->TxHostDescQPoolPhyAddr + align_offs));
	/* host_desc->desc_phy_next = (struct txhostdescriptor *) pDc->TxHostDescQPoolPhyAddr; */
#endif
	res = 0;

fail:
	/* dealloc will be done by free function on error case */
	FN_EXIT(1, res);
	return res;
}

static int acx111_create_tx_host_desc_queue(TIWLAN_DC *pDc)
{
	wlandevice_t *priv;

	UINT i;
	UINT16 eth_body_len = WLAN_MAX_ETHFRM_LEN - WLAN_ETHHDR_LEN;
#if (WLAN_HOSTIF!=WLAN_USB)
	UINT align_offs;
	UINT alignment;

	UINT8 *frame_buffer_phy;
	struct txhostdescriptor *host_desc_phy;
#endif

	UINT8 *frame_buffer;
	struct txhostdescriptor *host_desc;

	FN_ENTER;

	priv = pDc->priv;

	/* allocate TX buffer */
	pDc->TxBufferPoolSize = (priv->TxQueueCnt * sizeof(struct framehdr))
				+ priv->TxQueueCnt*2 * eth_body_len;
#if (WLAN_HOSTIF!=WLAN_USB)
	if (!(pDc->pTxBufferPool =
	      pci_alloc_consistent(0, pDc->TxBufferPoolSize, &pDc->TxBufferPoolPhyAddr))) {
		acxlog(L_BINSTD, "pDc->pTxBufferPool memory allocation error\n");
		FN_EXIT(1, 2);
		return 2;
	}
	acxlog(L_BINDEBUG, "pDc->TxBufferPoolSize = 0x%08x\n", (UINT) pDc->TxBufferPoolSize);
	acxlog(L_BINDEBUG, "pDc->TxBufferPool = 0x%08x\n", (UINT) pDc->pTxBufferPool);
	acxlog(L_BINDEBUG, "pDc->TxBufferPoolPhyAddr = 0x%08x\n", (UINT) pDc->TxBufferPoolPhyAddr);
#else
	if ((pDc->pTxBufferPool=kmalloc(pDc->TxBufferPoolSize,GFP_KERNEL))==NULL) {
		acxlog(L_STD,"pDc->pTxBufferPool memory allocation error\n");
		FN_EXIT(1,2);
		return(2);
	}
	memset(pDc->pTxBufferPool,0,pDc->TxBufferPoolSize);
#endif

	/* allocate the TX host descriptor queue pool */
	pDc->TxHostDescQPoolSize =  priv->TxQueueCnt*2 * sizeof(struct txhostdescriptor) + 3;
#if (WLAN_HOSTIF!=WLAN_USB)
	if (!(pDc->pTxHostDescQPool =
	      pci_alloc_consistent(0, pDc->TxHostDescQPoolSize,
				   &pDc->TxHostDescQPoolPhyAddr))) {
		acxlog(L_BINSTD, "Failed to allocate shared memory for TxHostDesc queue\n");
		pci_free_consistent(0, pDc->TxBufferPoolSize,
				    pDc->pTxBufferPool,
				    pDc->TxBufferPoolPhyAddr);
		FN_EXIT(1, 2);
		return 2;
	}
	acxlog(L_BINDEBUG, "pDc->pTxHostDescQPool = 0x%08x\n", (UINT) pDc->pTxHostDescQPool);
	acxlog(L_BINDEBUG, "pDc->TxHostDescQPoolPhyAddr = 0x%08x\n", (UINT) pDc->TxHostDescQPoolPhyAddr);
#else
	if ((pDc->pTxHostDescQPool=kmalloc(pDc->TxHostDescQPoolSize,GFP_KERNEL))==NULL) {
		acxlog(L_STD,"Failed to allocate memory for TxHostDesc queue\n");
		kfree(pDc->pTxBufferPool);
		FN_EXIT(1,2);
		return(2);
	}
	memset(pDc->pTxHostDescQPool,0,pDc->TxHostDescQPoolSize);
#endif

#if (WLAN_HOSTIF!=WLAN_USB)
	/* check for proper alignment of TX host descriptor pool */
	alignment = (UINT) pDc->pTxHostDescQPool & 3;
	if (alignment) {
		acxlog(L_BINSTD, "%s: TxHostDescQPool not aligned properly\n", __func__);
		align_offs = 4 - alignment;
	} else {
		align_offs = 0;
	}

	host_desc = (struct txhostdescriptor *) ((UINT8 *) pDc->pTxHostDescQPool + align_offs);
	host_desc_phy = (struct txhostdescriptor *) ((UINT8 *) pDc->TxHostDescQPoolPhyAddr + align_offs);
	frame_buffer_phy = (UINT8 *) pDc->TxBufferPoolPhyAddr;
#else
	host_desc=(struct txhostdescriptor *)pDc->pTxHostDescQPool;
#endif
	frame_buffer = (UINT8 *) pDc->pTxBufferPool;

	for (i = 0; i < priv->TxQueueCnt*2 - 1; i++)
	{

		host_desc->data = frame_buffer;
#if (WLAN_HOSTIF!=WLAN_USB)
		host_desc->data_phy = (ACX_PTR)frame_buffer_phy;
#endif
		if (!(i & 1)) {
			frame_buffer += sizeof(struct framehdr);
#if (WLAN_HOSTIF!=WLAN_USB)
			frame_buffer_phy += sizeof(struct framehdr);
			host_desc->pNext = (ACX_PTR)((UINT8 *) host_desc_phy + sizeof(struct txhostdescriptor));
#endif
		} else {
			frame_buffer += eth_body_len;
#if (WLAN_HOSTIF!=WLAN_USB)
			frame_buffer_phy += eth_body_len;
#endif
			host_desc->pNext = (ACX_PTR)NULL;
		}

		host_desc->Ctl_16 = cpu_to_le16(ACX100_CTL_OWN);

#if (WLAN_HOSTIF!=WLAN_USB)
		host_desc->desc_phy = host_desc_phy;
		host_desc->desc_phy_next = (ACX_PTR)((UINT8 *) host_desc_phy + sizeof(struct txhostdescriptor));
		host_desc_phy++;
#endif
		host_desc++;
	}
	host_desc->data = frame_buffer;
	/* TODO is this correct ?? */
	host_desc->pNext = (ACX_PTR)NULL;
	host_desc->Ctl_16 = cpu_to_le16(ACX100_CTL_OWN);

#if (WLAN_HOSTIF!=WLAN_USB)
	host_desc->data_phy = (ACX_PTR)frame_buffer_phy;
	host_desc->desc_phy = host_desc_phy;
	host_desc->desc_phy_next = (ACX_PTR)((UINT8 *) pDc->TxHostDescQPoolPhyAddr + align_offs);
#endif

	FN_EXIT(0, 0);
	return 0;
}

/*----------------------------------------------------------------
* acx100_create_rx_host_desc_queue
*
*
* Arguments:
*
* Returns:
*
* Side effects:
*
* Call context:
*
* STATUS:
*
* Comment:
*
*----------------------------------------------------------------*/

static int acx100_create_rx_host_desc_queue(TIWLAN_DC *pDc)
{
	wlandevice_t *priv;

	UINT i;
#if (WLAN_HOSTIF!=WLAN_USB)
	UINT align_offs;
	UINT alignment;

	struct rxhostdescriptor *host_desc_phy;
	struct rxbuffer *data_phy;
#endif

	struct rxhostdescriptor *host_desc;
	struct rxbuffer *data;

	int result = 2;

	FN_ENTER;

	priv = pDc->priv;

	/* allocate the RX host descriptor queue pool */
	pDc->RxHostDescQPoolSize = (priv->RxQueueCnt * sizeof(struct rxhostdescriptor)) + 0x3;

#if (WLAN_HOSTIF!=WLAN_USB)
	if (NULL == (pDc->pRxHostDescQPool =
	     pci_alloc_consistent(0, pDc->RxHostDescQPoolSize,
				&pDc->RxHostDescQPoolPhyAddr))) {
		acxlog(L_BINSTD,
		       "Failed to allocate shared memory for RxHostDesc queue\n");
		goto fail;
	}
#else
	if (NULL == (pDc->pRxHostDescQPool = kmalloc(pDc->RxHostDescQPoolSize, GFP_KERNEL))) {
		acxlog(L_STD,"Failed to allocate memory for RxHostDesc queue\n");
		goto fail;
	}
	memset(pDc->pRxHostDescQPool, 0, pDc->RxHostDescQPoolSize);
#endif

	/* allocate RX buffer pool */
	pDc->RxBufferPoolSize = ( priv->RxQueueCnt * (sizeof(struct rxbuffer) + 32) );
#if (WLAN_HOSTIF!=WLAN_USB)
	if (NULL == (pDc->pRxBufferPool =
	     pci_alloc_consistent(0, pDc->RxBufferPoolSize,
				  &pDc->RxBufferPoolPhyAddr))) {
		acxlog(L_BINSTD, "Failed to allocate shared memory for Rx buffer\n");
		goto fail;
	}
#else
	if (NULL == (pDc->pRxBufferPool = kmalloc(pDc->RxBufferPoolSize,GFP_KERNEL))) {
		acxlog(L_STD,"Failed to allocate memory for Rx buffer\n");
		goto fail;
	}
	memset(pDc->pRxBufferPool, 0, pDc->RxBufferPoolSize);
#endif

	acxlog(L_BINDEBUG, "pDc->pRxHostDescQPool = 0x%08x\n", (UINT) pDc->pRxHostDescQPool);
#if (WLAN_HOSTIF!=WLAN_USB)
	acxlog(L_BINDEBUG, "pDc->RxHostDescQPoolPhyAddr = 0x%08x\n", (UINT) pDc->RxHostDescQPoolPhyAddr);
#endif
	acxlog(L_BINDEBUG, "pDc->pRxBufferPool = 0x%08x\n", (UINT) pDc->pRxBufferPool);
#if (WLAN_HOSTIF!=WLAN_USB)
	acxlog(L_BINDEBUG, "pDc->RxBufferPoolPhyAddr = 0x%08x\n", (UINT) pDc->RxBufferPoolPhyAddr);
#endif
#if (WLAN_HOSTIF!=WLAN_USB)
	/* check for proper alignment of RX host descriptor pool */
	if ((alignment = ((UINT) pDc->pRxHostDescQPool) & 3)) {
		acxlog(L_BINSTD, "acx100_create_rx_host_desc_queue: RxHostDescQPool not aligned properly\n");
		align_offs = 4 - alignment;
	} else {
		align_offs = 0;
	}

	host_desc = (struct rxhostdescriptor *) ((UINT8 *) pDc->pRxHostDescQPool + align_offs);
	host_desc_phy = (struct rxhostdescriptor *) ((UINT8 *) pDc->RxHostDescQPoolPhyAddr + align_offs);

	priv->RxHostDescPoolStart = host_desc_phy;
#else
	host_desc = pDc->pRxHostDescQPool;
#endif
	data = (struct rxbuffer *)pDc->pRxBufferPool;
#if (WLAN_HOSTIF!=WLAN_USB)
	data_phy = (struct rxbuffer *) pDc->RxBufferPoolPhyAddr;
#endif

	for (i = 0; i < priv->RxQueueCnt - 1; i++) {
		host_desc->data = data;
		data++;
#if (WLAN_HOSTIF!=WLAN_USB)
		host_desc->data_phy = (ACX_PTR)cpu_to_le32(data_phy);
		data_phy++;
#endif
		host_desc->length = cpu_to_le16(sizeof(struct rxbuffer));

		/* FIXME: what do these mean ? 
		host_desc->val0x28 = 2;  */
		CLEAR_BIT(host_desc->Ctl_16, cpu_to_le16(ACX100_CTL_OWN));
#if (WLAN_HOSTIF!=WLAN_USB)
		host_desc->desc_phy = host_desc_phy;
		host_desc->desc_phy_next = (ACX_PTR)cpu_to_le32(((UINT8 *) host_desc_phy + sizeof(struct rxhostdescriptor)));
		host_desc_phy++;
#endif
		host_desc++;
	}
	host_desc->data = data;
#if (WLAN_HOSTIF!=WLAN_USB)
	host_desc->data_phy = (ACX_PTR)cpu_to_le32(data_phy);
#endif
	host_desc->length = cpu_to_le16(sizeof(struct rxbuffer) + 32);

/*	host_desc->val0x28 = 2; */
	CLEAR_BIT(host_desc->Ctl_16, cpu_to_le16(ACX100_CTL_OWN));
#if (WLAN_HOSTIF!=WLAN_USB)
	host_desc->desc_phy = host_desc_phy;
	host_desc->desc_phy_next = (ACX_PTR)cpu_to_le32(((UINT8 *) pDc->RxHostDescQPoolPhyAddr + align_offs));
#endif
	result = 0;

fail:
	/* dealloc will be done by free function on error case */
	FN_EXIT(1, result);
	return result;
}

/*----------------------------------------------------------------
* acx100_create_tx_desc_queue
*
*
* Arguments:
*
* Returns:
*
* Side effects:
*
* Call context:
*
* STATUS:
*
* Comment:
*
*----------------------------------------------------------------*/

/* optimized to nothing - it's a compile-time check, really */
extern void error_txdescriptor_must_be_0x30_bytes_in_length(void);
static void acx100_create_tx_desc_queue(TIWLAN_DC *pDc)
{
	wlandevice_t *priv;

	UINT32 mem_offs;
	UINT32 i;
	struct txdescriptor *tx_desc;
	struct txhostdescriptor *tx_hostdesc;

	UINT hostmemptr; /* remains 0 in USB case */

	FN_ENTER;

	priv = pDc->priv;
	pDc->tx_pool_count = priv->TxQueueCnt;

	pDc->TxDescrSize = sizeof(struct txdescriptor);

	if(sizeof(struct txdescriptor) != 0x30)
		error_txdescriptor_must_be_0x30_bytes_in_length();
	/* the acx111 txdescriptor is 4 bytes larger */
	if(priv->chip_type == CHIPTYPE_ACX111) {
		pDc->TxDescrSize = sizeof(struct txdescriptor)+4;
	}

#if (WLAN_HOSTIF==WLAN_USB)
  /* allocate memory for TxDescriptors */
	pDc->pTxDescQPool = kmalloc(pDc->tx_pool_count*pDc->TxDescrSize,GFP_KERNEL);
	if (!pDc->pTxDescQPool) {
		acxlog(L_STD,"Not enough memory to allocate txdescriptor queue\n");
		return;
	}
#endif

#if (WLAN_HOSTIF!=WLAN_USB)
	if(pDc->pTxDescQPool == NULL) { /* calculate it */
		pDc->pTxDescQPool = (struct txdescriptor *) (priv->iobase2 +
					     pDc->ui32ACXTxQueueStart);
	}

	acxlog(L_BINDEBUG, "priv->iobase2 = 0x%p\n", priv->iobase2);
	acxlog(L_BINDEBUG, "pDc->ui32ACXTxQueueStart = 0x%08x\n",
	       pDc->ui32ACXTxQueueStart);
	acxlog(L_BINDEBUG, "pDc->pTxDescQPool = 0x%08x\n",
	       (UINT32) pDc->pTxDescQPool);
#endif
	priv->TxQueueFree = priv->TxQueueCnt;
	pDc->tx_head = 0;
	pDc->tx_tail = 0;
	tx_desc = pDc->pTxDescQPool;
#if (WLAN_HOSTIF!=WLAN_USB)
	mem_offs = pDc->ui32ACXTxQueueStart;
	hostmemptr = pDc->TxHostDescQPoolPhyAddr;
#else
	mem_offs = (UINT32)pDc->pTxDescQPool;
	hostmemptr = 0; /* will remain 0 for USB */
#endif
	tx_hostdesc = (struct txhostdescriptor *) pDc->pTxHostDescQPool;

	/* clear whole send pool */
	memset(pDc->pTxDescQPool, 0, pDc->tx_pool_count * pDc->TxDescrSize);

	/* loop over whole send pool */
	for (i = 0; i < pDc->tx_pool_count; i++) {

		acxlog(L_BINDEBUG, "configure card tx descriptor = 0x%08x, size: 0x%X\n", (UINT32)tx_desc, pDc->TxDescrSize);

		/* pointer to hostdesc memory */
		tx_desc->HostMemPtr = cpu_to_le32(hostmemptr);
		/* initialise ctl */
		tx_desc->Ctl_8 = DESC_CTL_INIT;
		/* point to next txdesc */
		tx_desc->pNextDesc = cpu_to_le32(mem_offs + pDc->TxDescrSize);
		/* pointer to first txhostdesc */
		tx_desc->fixed_size.s.host_desc = tx_hostdesc;

		/* reserve two (hdr desc and payload desc) */
		tx_hostdesc += 2;
#if (WLAN_HOSTIF!=WLAN_USB)
		hostmemptr += 2 * sizeof(struct txhostdescriptor);
#endif
		/* go to the next */
		mem_offs += pDc->TxDescrSize;
		tx_desc = (struct txdescriptor *)(((UINT32)tx_desc) + pDc->TxDescrSize);
	}
	/* go to the last one */
	tx_desc = (struct txdescriptor *)(((UINT32)tx_desc) - pDc->TxDescrSize);
	/* tx_desc--; */
	/* and point to the first making it a ring buffer */
#if (WLAN_HOSTIF!=WLAN_USB)
	tx_desc->pNextDesc = cpu_to_le32(pDc->ui32ACXTxQueueStart);
#else
	tx_desc->pNextDesc = cpu_to_le32((UINT32)pDc->pTxDescQPool);
#endif
	FN_EXIT(0, 0);
}

/*----------------------------------------------------------------
* acx100_create_rx_desc_queue
*
*
* Arguments:
*
* Returns:
*
* Side effects:
*
* Call context:
*
* STATUS:
*
* Comment:
*
*----------------------------------------------------------------*/

static void acx100_create_rx_desc_queue(TIWLAN_DC *pDc)
{
	wlandevice_t *priv;
#if (WLAN_HOSTIF!=WLAN_USB)
	UINT32 mem_offs;
	UINT32 i;
	struct rxdescriptor *rx_desc;
#endif

	FN_ENTER;

	priv = pDc->priv;

#if (WLAN_HOSTIF!=WLAN_USB)
	if(pDc->pRxDescQPool == NULL) { /* calculate it */
		/* WHY IS IT "TxQueueCnt"?
		 * Probably because Rx pool ptr should be right AFTER Tx pool */
		pDc->pRxDescQPool = (struct rxdescriptor *) ((UINT8 *) pDc->pTxDescQPool + (priv->TxQueueCnt * sizeof(struct txdescriptor)));
	}
#endif
	pDc->rx_pool_count = priv->RxQueueCnt;
	pDc->rx_tail = 0;
#if (WLAN_HOSTIF!=WLAN_USB)
	mem_offs = pDc->ui32ACXRxQueueStart;
	rx_desc = pDc->pRxDescQPool;

	/* clear whole receive pool */
	memset(pDc->pRxDescQPool, 0, pDc->rx_pool_count * sizeof(struct rxdescriptor));

	/* loop over whole receive pool */
	for (i = 0; i < pDc->rx_pool_count; i++) {

		acxlog(L_BINDEBUG, "configure card rx descriptor = 0x%08x\n", (UINT32)rx_desc);

		if(priv->chip_type == CHIPTYPE_ACX100) {
			rx_desc->Ctl_8 = 0xc;
		} else if(priv->chip_type == CHIPTYPE_ACX111) {
			rx_desc->Ctl_8 = 0x0;
		}

		/* point to next rxdesc */
		rx_desc->pNextDesc = cpu_to_le32(mem_offs + sizeof(struct rxdescriptor)); /* next rxdesc pNextDesc */

		/* go to the next */
		mem_offs += sizeof(struct rxdescriptor);
		rx_desc++;
	}
	/* go to the last one */
	rx_desc--;

	/* and point to the first making it a ring buffer */
	rx_desc->pNextDesc = cpu_to_le32(pDc->ui32ACXRxQueueStart);
#endif
	FN_EXIT(0, 0);
}

/*----------------------------------------------------------------
* acx100_init_memory_pools
*
*
* Arguments:
*
* Returns:
*
* Side effects:
*
* Call context:
*
* STATUS:
*
* Comment:
*	FIXME: This function still needs a cleanup
*----------------------------------------------------------------*/

static int acx100_init_memory_pools(wlandevice_t *priv, acx100_ie_memmap_t *mmt)
{
	UINT32 TotalMemoryBlocks;
        acx100_ie_memblocksize_t MemoryBlockSize;

        acx100_ie_memconfigoption_t MemoryConfigOption;

	FN_ENTER;


	/* Let's see if we can follow this:
	   first we select our memory block size (which I think is
	   completely arbitrary) */
	MemoryBlockSize.size = cpu_to_le16(priv->memblocksize);

	/* Then we alert the card to our decision of block size */
	if (OK != acx100_configure(priv, &MemoryBlockSize, ACX100_IE_BLOCK_SIZE)) {
		acxlog(L_BINSTD, "Ctl: MemoryBlockSizeWrite failed\n");
		FN_EXIT(1, NOT_OK);
		return NOT_OK;
	}

	/* We figure out how many total blocks we can create, using
	   the block size we chose, and the beginning and ending
	   memory pointers. IE. end-start/size */
	TotalMemoryBlocks = (le32_to_cpu(mmt->PoolEnd) - le32_to_cpu(mmt->PoolStart)) / priv->memblocksize;

	acxlog(L_DEBUG,"TotalMemoryBlocks=%d (%d bytes)\n",TotalMemoryBlocks,TotalMemoryBlocks*priv->memblocksize);

	/* This one I have no idea on */
	/* block-transfer=0x20000
	 * indirect descriptors=0x10000
	 */
#if (WLAN_HOSTIF==WLAN_USB)
	MemoryConfigOption.DMA_config = cpu_to_le32(0x20000);
#else
	MemoryConfigOption.DMA_config = cpu_to_le32(0x30000);
#endif


	/* Declare start of the Rx host pool */
	MemoryConfigOption.pRxHostDesc = cpu_to_le32((UINT32)priv->RxHostDescPoolStart);
	acxlog(L_DEBUG, "pRxHostDesc %08x, RxHostDescPoolStart %p\n", MemoryConfigOption.pRxHostDesc, priv->RxHostDescPoolStart);

	/* 50% of the allotment of memory blocks go to tx descriptors */
	priv->TxBlockNum = TotalMemoryBlocks / 2;
	MemoryConfigOption.TxBlockNum = cpu_to_le16(priv->TxBlockNum);

	/* and 50% go to the rx descriptors */
	priv->RxBlockNum = TotalMemoryBlocks - priv->TxBlockNum;
	MemoryConfigOption.RxBlockNum = cpu_to_le16(priv->RxBlockNum);

	/* size of the tx and rx descriptor queues */
	priv->TotalTxBlockSize = priv->TxBlockNum * priv->memblocksize;
	priv->TotalRxBlockSize = priv->RxBlockNum * priv->memblocksize;
	acxlog(L_DEBUG, "TxBlockNum %d RxBlockNum %d TotalTxBlockSize %d TotalTxBlockSize %d\n", MemoryConfigOption.TxBlockNum, MemoryConfigOption.RxBlockNum, priv->TotalTxBlockSize, priv->TotalRxBlockSize);


	/* align the tx descriptor queue to an alignment of 0x20 (32 bytes) */
	MemoryConfigOption.rx_mem =
		cpu_to_le32((le32_to_cpu(mmt->PoolStart) + 0x1f) & ~0x1f);

	/* align the rx descriptor queue to units of 0x20 and offset it
	   by the tx descriptor queue */
	MemoryConfigOption.tx_mem =
	    cpu_to_le32((le32_to_cpu(mmt->PoolStart) + priv->TotalRxBlockSize + 0x1f) & ~0x1f);
	acxlog(L_DEBUG, "rx_mem %08x rx_mem %08x\n", MemoryConfigOption.tx_mem, MemoryConfigOption.rx_mem);

	/* alert the device to our decision */
	if (OK != acx100_configure(priv, &MemoryConfigOption, ACX1xx_IE_MEMORY_CONFIG_OPTIONS)) {
		acxlog(L_DEBUG,"%s: configure memory config options failed!\n", __func__);
		FN_EXIT(1, NOT_OK);
		return NOT_OK;
	}

	/* and tell the device to kick it into gear */
	if (OK != acx100_issue_cmd(priv, ACX100_CMD_INIT_MEMORY, NULL, 0, 5000)) {
		acxlog(L_DEBUG,"%s: init memory failed!\n", __func__);
		FN_EXIT(1, NOT_OK);
		return NOT_OK;
	}
	FN_EXIT(1, OK);
	return OK;
}

/*----------------------------------------------------------------
* acx100_delete_dma_regions
*
*
* Arguments:
*
* Returns:
*
* Side effects:
*
* Call context:
*
* STATUS:
*
* Comment:
*
*----------------------------------------------------------------*/

int acx100_delete_dma_regions(wlandevice_t *priv)
{
	FN_ENTER;
#if (WLAN_HOSTIF!=WLAN_USB)
	/* disable radio Tx/Rx. Shouldn't we use the firmware commands
	 * here instead? Or are we that much down the road that it's no
	 * longer possible here? */
	acx100_write_reg16(priv, priv->io[IO_ACX_ENABLE], 0);

	/* used to be a for loop 1000, do scheduled delay instead */
	acx100_schedule(HZ / 10);
#endif
	acx100_free_desc_queues(&priv->dc);

	FN_EXIT(0, OK);
	return OK;
}

/*----------------------------------------------------------------
* acx100_create_dma_regions
*
*
* Arguments:
*
* Returns:
*
* Side effects:
*
* Call context:
*
* STATUS:
*
* Comment:
*
*----------------------------------------------------------------*/

int acx100_create_dma_regions(wlandevice_t *priv)
{
	acx100_ie_queueconfig_t qcfg;
	
	acx100_ie_memmap_t MemMap;
	struct TIWLAN_DC *pDc;
	int res = NOT_OK;

	FN_ENTER;

	pDc = &priv->dc;
	pDc->priv = priv;
	spin_lock_init(&pDc->rx_lock);
	spin_lock_init(&pDc->tx_lock);


	/* read out the acx100 physical start address for the queues */
	if (OK != acx100_interrogate(priv, &MemMap, ACX1xx_IE_MEMORY_MAP)) {
		acxlog(L_BINSTD, "ctlMemoryMapRead returns error\n");
		goto fail;
	}

	/* # of items in Rx and Tx queues */
	priv->TxQueueCnt = TXBUFFERCOUNT;
	priv->RxQueueCnt = RXBUFFERCOUNT;

	/* calculate size of queues */
	qcfg.AreaSize = cpu_to_le32(
			(sizeof(struct txdescriptor) * TXBUFFERCOUNT +
	                 sizeof(struct rxdescriptor) * RXBUFFERCOUNT + 8)
			);
	qcfg.NumTxQueues = 1;  /* number of tx queues */

	/* sets the beginning of the tx descriptor queue */
	pDc->ui32ACXTxQueueStart = le32_to_cpu(MemMap.QueueStart);
	qcfg.TxQueueStart = cpu_to_le32(pDc->ui32ACXTxQueueStart);
	qcfg.TxQueuePri = 0;

#if (WLAN_HOSTIF==WLAN_USB)
	qcfg.NumTxDesc = TXBUFFERCOUNT;
	qcfg.NumRxDesc = RXBUFFERCOUNT;
#endif

	/* sets the beginning of the rx descriptor queue, after the tx descrs */
	pDc->ui32ACXRxQueueStart = priv->TxQueueCnt * sizeof(struct txdescriptor) + le32_to_cpu(MemMap.QueueStart);
	qcfg.RxQueueStart = cpu_to_le32(pDc->ui32ACXRxQueueStart);
	qcfg.QueueOptions = 1;		/* auto reset descriptor */

	/* sets the end of the rx descriptor queue */
	qcfg.QueueEnd = cpu_to_le32(priv->RxQueueCnt * sizeof(struct rxdescriptor) + pDc->ui32ACXRxQueueStart);

	/* sets the beginning of the next queue */
	qcfg.HostQueueEnd = cpu_to_le32(le32_to_cpu(qcfg.QueueEnd) + 8);

	acxlog(L_BINDEBUG, "<== Initialize the Queue Indicator\n");

	if (OK != acx100_configure_length(priv, &qcfg, ACX1xx_IE_QUEUE_CONFIG, sizeof(acx100_ie_queueconfig_t)-4)){ /* 0x14 + (qcfg.vale * 8))) { */
		acxlog(L_BINSTD, "ctlQueueConfigurationWrite returns fail\n");
		goto fail;
	}

	if (OK != acx100_create_tx_host_desc_queue(pDc)) {
		acxlog(L_BINSTD, "acx100_create_tx_host_desc_queue returns fail\n");
		goto fail;
	}
	if (OK != acx100_create_rx_host_desc_queue(pDc)) {
		acxlog(L_BINSTD, "acx100_create_rx_host_desc_queue returns fail\n");
		goto fail;
	}

	pDc->pTxDescQPool = NULL;
	pDc->pRxDescQPool = NULL;

	acx100_create_tx_desc_queue(pDc);
	acx100_create_rx_desc_queue(pDc);
	if (OK != acx100_interrogate(priv, &MemMap, ACX1xx_IE_MEMORY_MAP)) {
		acxlog(L_BINSTD, "Failed to read memory map\n");
		goto fail;
	}

#if (WLAN_HOSTIF==WLAN_USB)
	if (OK != acx100_configure(priv, &MemMap, ACX1xx_IE_MEMORY_MAP)) {
		acxlog(L_BINSTD,"Failed to write memory map\n");
		goto fail;
	}
#endif

	MemMap.PoolStart = cpu_to_le32((le32_to_cpu(MemMap.QueueEnd) + 0x1f + 4) & ~0x1f);

	if (OK != acx100_configure(priv, &MemMap, ACX1xx_IE_MEMORY_MAP)) {
		acxlog(L_BINSTD, "ctlMemoryMapWrite returns fail\n");
		goto fail;
	}

	if (OK != acx100_init_memory_pools(priv, (acx100_ie_memmap_t *) &MemMap)) {
		acxlog(L_BINSTD, "acx100_init_memory_pools returns fail\n");
		goto fail;
	}

	res = OK;
	goto ok;

fail:
	acxlog(0xffff, "dma error!!\n");
	acx100_schedule(10 * HZ);
	acx100_free_desc_queues(pDc);

ok:
	FN_EXIT(1, res);
	return res;
}


/*----------------------------------------------------------------
* acx111_create_dma_regions
*
*
* Arguments:
*
* Returns:
*
* Side effects:
*
* Call context:
*
* STATUS:
*
* Comment:
*
*----------------------------------------------------------------*/

int acx111_create_dma_regions(wlandevice_t *priv)
{
	struct acx111_ie_memoryconfig memconf;
	struct acx111_ie_queueconfig queueconf;
	struct TIWLAN_DC *pDc;

	FN_ENTER;

	pDc = &priv->dc;
	pDc->priv = priv;
	spin_lock_init(&pDc->rx_lock);
	spin_lock_init(&pDc->tx_lock);

	/* ### Calculate memory positions and queue sizes #### */

	priv->TxQueueCnt = TXBUFFERCOUNT;
	priv->RxQueueCnt = RXBUFFERCOUNT;

	acxlog(L_BINDEBUG, "<== Initialize the Queue Indicator\n");


	/* ### Set up the card #### */

	memset(&memconf, 0, sizeof(memconf));

	/* hm, I hope this is correct */
	memconf.no_of_stations = cpu_to_le16(1);

	/* specify the memory block size. Default is 256 */
	memconf.memory_block_size = cpu_to_le16(priv->memblocksize); 	

	/* let's use 50%/50% for tx/rx */
	memconf.tx_rx_memory_block_allocation = 10;


	/* set the count of our queues */
	memconf.count_rx_queues = 1; /* TODO place this in constants */
	memconf.count_tx_queues = 1;

	if ((memconf.count_rx_queues != 1) || (memconf.count_tx_queues != 1)) {
		acxlog(L_STD, 
			"%s: Requested more queues than supported.  Please adapt the structure! rxbuffers:%d txbuffers:%d\n", 
			__func__, memconf.count_rx_queues, memconf.count_tx_queues);
		goto fail;
	}

	memconf.options = 0; /* 0 == Busmaster Indirect Memory Organization, which is what we want (using linked host descs with their allocated mem). 2 == Generic Bus Slave */

	/* let's use 25% for fragmentations and 75% for frame transfers */
	memconf.fragmentation = 0x0f; 

	/* Set up our host descriptor pool + data pool */
	if (OK != acx111_create_tx_host_desc_queue(pDc)) {
		acxlog(L_BINSTD, "acx111_create_tx_host_desc_queue returns fail\n");
		goto fail;
	}
	if (OK != acx100_create_rx_host_desc_queue(pDc)) {
		acxlog(L_BINSTD, "acx100_create_rx_host_desc_queue returns fail\n");
		goto fail;
	}

	/* RX descriptor queue config */
	memconf.rx_queue1_count_descs = RXBUFFERCOUNT;
	memconf.rx_queue1_reserved2 = 7; /* must be set to 7 */
	memconf.rx_queue1_host_rx_start = pDc->RxHostDescQPoolPhyAddr;

	/* TX descriptor queue config */
	memconf.tx_queue1_count_descs = TXBUFFERCOUNT;
	memconf.tx_queue1_attributes = 15; /* highest priority */

	acxlog(L_STD, "%s: set up acx111 queue memory configuration (queue configs + descriptors)\n", __func__);
	if (OK != acx100_configure(priv, &memconf, ACX1xx_IE_QUEUE_CONFIG /*0x03*/)) {
		acxlog(L_STD, "setting up the memory configuration failed!\n");
		goto fail;
	}

	/* read out queueconfig */
	memset(&queueconf, 0xff, sizeof(queueconf));

	if (OK != acx100_interrogate(priv, &queueconf, ACX1xx_IE_MEMORY_CONFIG_OPTIONS /*0x05*/)) {
		acxlog(L_BINSTD, "read queuehead returns fail\n");
	}

	acxlog(L_BINSTD, "dump queue head:\n");
	acxlog(L_BINSTD, "length: %d\n", queueconf.len);
	acxlog(L_BINSTD, "tx_memory_block_address (from card): %X\n", queueconf.tx_memory_block_address);
	acxlog(L_BINSTD, "rx_memory_block_address (from card): %X\n", queueconf.rx_memory_block_address);

	acxlog(L_BINSTD, "rx1_queue address (from card): %X\n", queueconf.rx1_queue_address);
	acxlog(L_BINSTD, "tx1_queue address (from card): %X\n", queueconf.tx1_queue_address);

	pDc->pTxDescQPool = (struct txdescriptor *) (priv->iobase2 +
				     queueconf.tx1_queue_address);
	pDc->ui32ACXTxQueueStart = queueconf.tx1_queue_address;

	pDc->pRxDescQPool = (struct rxdescriptor *) (priv->iobase2 +
				     queueconf.rx1_queue_address);
	pDc->ui32ACXRxQueueStart = queueconf.rx1_queue_address;

	acx100_create_tx_desc_queue(pDc);
	acx100_create_rx_desc_queue(pDc);

	FN_EXIT(1, OK);
	return OK;

fail:
	acx100_free_desc_queues(pDc);

	FN_EXIT(1, NOT_OK);
	return NOT_OK;
}


/*----------------------------------------------------------------
* acx100_get_tx_desc
*
*
* Arguments:
*
* Returns:
*
* Side effects:
*
* Call context:
*
* STATUS:
*
* Comment:
*
*----------------------------------------------------------------*/

struct txdescriptor *acx100_get_tx_desc(wlandevice_t *priv)
{
	struct TIWLAN_DC *pDc = &priv->dc;
	struct txdescriptor *tx_desc;
	unsigned long flags;

	FN_ENTER;

	spin_lock_irqsave(&pDc->tx_lock, flags);

	tx_desc = GET_TX_DESC_PTR(pDc, pDc->tx_head);

	if (unlikely(0 == (tx_desc->Ctl_8 & ACX100_CTL_OWN))) {
		/* whoops, descr at current index is not free, so probably
		 * ring buffer already full */
		tx_desc = NULL;
		goto fail;
	}

	priv->TxQueueFree--;
	acxlog(L_BUFT, "tx: got desc %d, %d remain\n", pDc->tx_head, priv->TxQueueFree);

/*
 * This comment is probably not entirely correct, needs further discussion
 * (restored commented-out code below to fix Tx ring buffer overflow,
 * since it's much better to have a slightly less efficiently used ring
 * buffer rather than one which easily overflows):
 *
 * This doesn't do anything other than limit our maximum number of
 * buffers used at a single time (we might as well just declare
 * MINFREE_TX less descriptors when we open up.) We should just let it
 * slide here, and back off MINFREE_TX in acx100_clean_tx_desc, when given the
 * opportunity to let the queue start back up.
 */
	if (priv->TxQueueFree < MINFREE_TX)
	{
		acxlog(L_BUF, "stop queue (avail. Tx desc %d).\n", priv->TxQueueFree);
		netif_stop_queue(priv->netdev);
	}

	/* returning current descriptor, so advance to next free one */
	pDc->tx_head = (pDc->tx_head + 1) % pDc->tx_pool_count;
fail:
	spin_unlock_irqrestore(&pDc->tx_lock, flags);

	FN_EXIT(0, (int)tx_desc);
	return tx_desc;
}

static char type_string[32];	/* I *really* don't care that this is static,
				   so don't complain, else... ;-) */

/*----------------------------------------------------------------
* acx100_get_packet_type_string
*
*
* Arguments:
*
* Returns:
*
* Side effects:
*
* Call context:
*
* STATUS:
*
* Comment:
*
*----------------------------------------------------------------*/

char *acx100_get_packet_type_string(UINT16 fc)
{
	char *ftype = "UNKNOWN", *fstype = "UNKNOWN";
	char *mgmt_arr[] = { "AssocReq", "AssocResp", "ReassocReq", "ReassocResp", "ProbeReq", "ProbeResp", "UNKNOWN", "UNKNOWN", "Beacon", "ATIM", "Disassoc", "Authen", "Deauthen" };
	char *ctl_arr[] = { "PSPoll", "RTS", "CTS", "Ack", "CFEnd", "CFEndCFAck" };
	char *data_arr[] = { "DataOnly", "Data CFAck", "Data CFPoll", "Data CFAck/CFPoll", "Null", "CFAck", "CFPoll", "CFAck/CFPoll" };

	FN_ENTER;
	switch (WLAN_GET_FC_FTYPE(fc)) {
	case WLAN_FTYPE_MGMT:
		ftype = "MGMT";
		if (WLAN_GET_FC_FSTYPE(fc) <= 0x0c)
			fstype = mgmt_arr[WLAN_GET_FC_FSTYPE(fc)];
		break;
	case WLAN_FTYPE_CTL:
		ftype = "CTL";
		if ((WLAN_GET_FC_FSTYPE(fc) >= 0x0a) && (WLAN_GET_FC_FSTYPE(fc) <= 0x0f))
			fstype = ctl_arr[WLAN_GET_FC_FSTYPE(fc)-0x0a];
		break;
	case WLAN_FTYPE_DATA:
		ftype = "DATA";
		if (WLAN_GET_FC_FSTYPE(fc) <= 0x07)
			fstype = data_arr[WLAN_GET_FC_FSTYPE(fc)];
		break;
	}
	sprintf(type_string, "%s/%s", ftype, fstype);
	FN_EXIT(1, (int)type_string);
	return type_string;
}

void acx100_dump_bytes(void *data,int num)
{
  int i,remain=num;
  unsigned char *ptr=(unsigned char *)data;

  if (!(debug & L_DEBUG))
	  return;

  while (remain>0) {
    printk(KERN_WARNING);
    if (remain<16) {
      for (i=0;i<remain;i++) printk("%02X ",*ptr++);
      remain=0;
    } else {
      for (i=0;i<16;i++) printk("%02X ",*ptr++);
      remain-=16;
    }
    printk("\n");
  }
}

