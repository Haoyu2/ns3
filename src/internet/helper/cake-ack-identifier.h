/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef CAKE_ACK_IDENTIFIER_H
#define CAKE_ACK_IDENTIFIER_H

#include "ns3/cake-queue-disc.h"

namespace ns3
{

/**
 * @ingroup internet
 *
 * @brief Build an ACK identifier for CAKE's ACK filtering that understands TCP.
 *
 * The returned callback recognises pure (data-less) IPv4 TCP ACKs and extracts
 * their acknowledgement number. It is meant to be passed to
 * CakeQueueDisc::SetAckIdentifier so that, in real simulations, CAKE can thin
 * superseded ACKs. Header parsing lives here (in the internet module) so that
 * the CAKE model in the traffic-control module stays free of TCP/IP types.
 *
 * @note IPv6 is not yet handled; such packets are reported as non-ACKs.
 *
 * @return an AckIdentifier suitable for CakeQueueDisc::SetAckIdentifier
 */
CakeQueueDisc::AckIdentifier MakeTcpAckIdentifier();

/**
 * @ingroup internet
 *
 * @brief Build a host classifier for CAKE's host-fair ("triple isolation")
 * scheduling that keys hosts by their IPv4 addresses.
 *
 * The returned callback sets the source/destination host keys to the 32-bit
 * IPv4 source/destination addresses of the item. Pass it to
 * CakeQueueDisc::SetHostClassifier. Non-IPv4 items yield zero keys.
 *
 * @return a HostClassifier suitable for CakeQueueDisc::SetHostClassifier
 */
CakeQueueDisc::HostClassifier MakeIpv4HostClassifier();

} // namespace ns3

#endif /* CAKE_ACK_IDENTIFIER_H */
