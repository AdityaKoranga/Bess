// Copyright (c) 2017, The Regents of the University of California.
// Copyright (c) 2017, Nefeli Networks, Inc.
// All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// * Neither the names of the copyright holders nor the names of their
// contributors may be used to endorse or promote products derived from this
// software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include "l4_checksum.h"

#include "../utils/checksum.h"
#include "../utils/ether.h"
#include "../utils/ip.h"
#include "../utils/tcp.h"
#include "../utils/udp.h"

enum { FORWARD_GATE = 0, FAIL_GATE };

void L4Checksum::ProcessBatch(Context *ctx, bess::PacketBatch *batch) {
  using bess::utils::be16_t;
  using bess::utils::Ethernet;
  using bess::utils::Ipv4;
  using bess::utils::Tcp;
  using bess::utils::Udp;

  int cnt = batch->cnt();

  for (int i = 0; i < cnt; i++) {
    Ethernet *eth = batch->pkts()[i]->head_data<Ethernet *>();

    // Calculate checksum only for IPv4 packets
    if (eth->ether_type != be16_t(Ethernet::Type::kIpv4)) {
      EmitPacket(ctx, batch->pkts()[i], FORWARD_GATE);
      continue;
    }

    Ipv4 *ip = reinterpret_cast<Ipv4 *>(eth + 1);

    if (ip->protocol == Ipv4::Proto::kUdp) {
      size_t ip_bytes = (ip->header_length) << 2;
      Udp *udp =
          reinterpret_cast<Udp *>(reinterpret_cast<uint8_t *>(ip) + ip_bytes);
      if (verify_) {
        if (hw_) {
          struct rte_mbuf *m = (struct rte_mbuf *)batch->pkts()[i];
          if (unlikely((m->ol_flags & PKT_RX_L4_CKSUM_MASK) ==
                       PKT_RX_L4_CKSUM_BAD))
            EmitPacket(ctx, (bess::Packet *)m, FAIL_GATE);
          else
            EmitPacket(ctx, (bess::Packet *)m, FORWARD_GATE);
        } else {
          EmitPacket(
              ctx, batch->pkts()[i],
              (VerifyIpv4UdpChecksum(*ip, *udp)) ? FORWARD_GATE : FAIL_GATE);
        }
      } else {
        udp->checksum = CalculateIpv4UdpChecksum(*ip, *udp);
        EmitPacket(ctx, batch->pkts()[i], FORWARD_GATE);
      }
    } else if (ip->protocol == Ipv4::Proto::kTcp) {
      size_t ip_bytes = (ip->header_length) << 2;
      Tcp *tcp =
          reinterpret_cast<Tcp *>(reinterpret_cast<uint8_t *>(ip) + ip_bytes);
      if (verify_) {
        if (hw_) {
          struct rte_mbuf *m = (struct rte_mbuf *)batch->pkts()[i];
          if (unlikely((m->ol_flags & PKT_RX_L4_CKSUM_MASK) ==
                       PKT_RX_L4_CKSUM_BAD))
            EmitPacket(ctx, (bess::Packet *)m, FAIL_GATE);
          else
            EmitPacket(ctx, (bess::Packet *)m, FORWARD_GATE);
        } else {
          EmitPacket(
              ctx, batch->pkts()[i],
              (VerifyIpv4TcpChecksum(*ip, *tcp)) ? FORWARD_GATE : FAIL_GATE);
        }
      } else {
        tcp->checksum = CalculateIpv4TcpChecksum(*ip, *tcp);
        EmitPacket(ctx, batch->pkts()[i], FORWARD_GATE);
      }
    } else { /* fail-safe condition */
      EmitPacket(ctx, batch->pkts()[i], FORWARD_GATE);
    }
  }
}

CommandResponse L4Checksum::Init(const bess::pb::L4ChecksumArg &arg) {
  verify_ = arg.verify();
  hw_ = arg.hw();
  return CommandSuccess();
}

ADD_MODULE(L4Checksum, "l4_checksum",
           "recomputes the TCP/Ipv4 and UDP/IPv4 checksum")
