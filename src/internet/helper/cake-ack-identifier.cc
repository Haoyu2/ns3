/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "cake-ack-identifier.h"

#include "ns3/ipv4-queue-disc-item.h"
#include "ns3/packet.h"
#include "ns3/tcp-header.h"

namespace ns3
{

CakeQueueDisc::AckIdentifier
MakeTcpAckIdentifier()
{
    return [](Ptr<const QueueDiscItem> item, uint32_t& ackNo) -> bool {
        // CAKE sits below IP, so the IPv4 header is carried in the item (not in
        // the packet buffer); the TCP header is at the front of the packet.
        auto ipItem = DynamicCast<const Ipv4QueueDiscItem>(item);
        if (!ipItem)
        {
            return false; // not IPv4 (IPv6 not handled yet)
        }
        if (ipItem->GetHeader().GetProtocol() != 6)
        {
            return false; // not TCP
        }

        Ptr<Packet> copy = item->GetPacket()->Copy();
        TcpHeader tcp;
        if (copy->PeekHeader(tcp) == 0)
        {
            return false;
        }

        // A pure ACK carries no payload: the packet is exactly the TCP header.
        if (copy->GetSize() != static_cast<uint32_t>(tcp.GetLength()) * 4u)
        {
            return false;
        }

        uint8_t flags = tcp.GetFlags();
        if ((flags & TcpHeader::ACK) == 0)
        {
            return false;
        }
        // Only thin steady-state ACKs; never touch connection-control segments.
        if (flags & (TcpHeader::SYN | TcpHeader::FIN | TcpHeader::RST))
        {
            return false;
        }

        ackNo = tcp.GetAckNumber().GetValue();
        return true;
    };
}

CakeQueueDisc::HostClassifier
MakeIpv4HostClassifier()
{
    return [](Ptr<const QueueDiscItem> item, uint32_t& srcHost, uint32_t& dstHost) {
        srcHost = 0;
        dstHost = 0;
        auto ipItem = DynamicCast<const Ipv4QueueDiscItem>(item);
        if (ipItem)
        {
            srcHost = ipItem->GetHeader().GetSource().Get();
            dstHost = ipItem->GetHeader().GetDestination().Get();
        }
    };
}

} // namespace ns3
