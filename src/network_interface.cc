#include <iostream>

#include "arp_message.hh"
#include "debug.hh"
#include "ethernet_frame.hh"
#include "exception.hh"
#include "helpers.hh"
#include "network_interface.hh"

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface( string_view name,
                                    shared_ptr<OutputPort> port,
                                    const EthernetAddress& ethernet_address,
                                    const Address& ip_address )
  : name_( name )
  , port_( notnull( "OutputPort", move( port ) ) )
  , ethernet_address_( ethernet_address )
  , ip_address_( ip_address )
{
  cerr << "DEBUG: Network interface has Ethernet address " << to_string( ethernet_address_ ) << " and IP address "
       << ip_address.ip() << "\n";
}

EthernetFrame NetworkInterface::make_eth_frame_for_ip(EthernetAddress dst, const InternetDatagram& dgram) {
    EthernetHeader head {};
    head.type = EthernetHeader::TYPE_IPv4;
    head.src = ethernet_address_;
    head.dst = dst;
    EthernetFrame frame {};
    frame.header = head;
    Serializer s {};
    dgram.serialize(s);
    frame.payload = s.finish();
    return frame;
  }

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but
//! may also be another host if directly connected to the same network as the destination) Note: the Address type
//! can be converted to a uint32_t (raw 32-bit IP address) by using the Address::ipv4_numeric() method.
void NetworkInterface::send_datagram( const InternetDatagram& dgram, const Address& next_hop )
{
  uint32_t dst_ip = next_hop.ipv4_numeric();
  if (arp_table_.count(dst_ip)) {
    //目标以太网地址已知，立即发送
    transmit(make_eth_frame_for_ip(arp_table_[dst_ip].first, dgram));
  } else {
    //如果目标以太网地址未知，广播一个针对下一跳以太网地址的 ARP 请求
    //并将 IP 数据报排队，以便在收到 ARP 回复后发送。
    if (waited_dgram_.count(dst_ip)) {
      waited_dgram_[dst_ip].first.push_back(std::move(dgram));
    } else {
      ARPMessage msg {};
      msg.opcode = ARPMessage::OPCODE_REQUEST;
      msg.sender_ip_address = ip_address_.ipv4_numeric();
      msg.sender_ethernet_address = ethernet_address_;
      msg.target_ip_address = dst_ip;
      // 设置为全零，表示未知
      msg.target_ethernet_address = {};
      EthernetHeader head {};
      head.type = EthernetHeader::TYPE_ARP;
      head.src = ethernet_address_;
      head.dst = ETHERNET_BROADCAST;
      EthernetFrame frame {};
      frame.header = head;
      Serializer s {};
      msg.serialize(s);
      frame.payload = s.finish();
      transmit(frame);
      waited_dgram_[dst_ip] = {{std::move(dgram)}, 0};
    }
  }
}

//! \param[in] frame the incoming Ethernet frame
void NetworkInterface::recv_frame( EthernetFrame frame )
{
  //忽略任何不是发往该网络接口的帧
  if (frame.header.dst != ETHERNET_BROADCAST && frame.header.dst != ethernet_address_) {
    return ;
  }
  
  // 这是个ARP请求
  if (frame.header.type == EthernetHeader::TYPE_ARP) {
    Parser parser (frame.payload);
    ARPMessage msg {};
    msg.parse(parser);
    if (parser.has_error()) {
      return ;
    }
    auto it = arp_table_.find(msg.sender_ip_address);
    if (it == arp_table_.end()) {
      arp_table_[msg.sender_ip_address] = {msg.sender_ethernet_address, 0};
    } else {
      // 有效时间重置
      it->second.second = 0;
    }

    if (msg.opcode == ARPMessage::OPCODE_REQUEST) {
      if (msg.target_ip_address == ip_address_.ipv4_numeric()) {
        ARPMessage reply_msg {};
        reply_msg.opcode = ARPMessage::OPCODE_REPLY;
        reply_msg.sender_ip_address = ip_address_.ipv4_numeric();
        reply_msg.sender_ethernet_address = ethernet_address_;
        reply_msg.target_ip_address = msg.sender_ip_address;
        reply_msg.target_ethernet_address = msg.sender_ethernet_address;
        EthernetHeader head {};
        head.type = EthernetHeader::TYPE_ARP;
        head.src = ethernet_address_;
        head.dst = msg.sender_ethernet_address;
        EthernetFrame reply_frame {};
        reply_frame.header = head;
        Serializer s {};
        reply_msg.serialize(s);
        reply_frame.payload = s.finish();
        transmit(reply_frame);
      }
    } else {
      for (auto &x : waited_dgram_[msg.sender_ip_address].first) {
        send_datagram(x, Address::from_ipv4_numeric(msg.sender_ip_address));
      }
      waited_dgram_.erase(msg.sender_ip_address);
    }
  } else { //IP 请求
    Parser parser (frame.payload);
    InternetDatagram dgram {};
    dgram.parse(parser);
    if (parser.has_error()) {
      return ;
    }
    datagrams_received_.push(std::move(dgram));
  }
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick( const size_t ms_since_last_tick )
{
  {
    auto it = arp_table_.begin();
    while(it != arp_table_.end()) {
      it->second.second += ms_since_last_tick;
      if (it->second.second >= ip_overtime_) {
        it = arp_table_.erase(it);
      } else {
        it++;
      }
    }
  }
  {
    auto it = waited_dgram_.begin();
    while(it != waited_dgram_.end()) {
      it->second.second += ms_since_last_tick;
      if (it->second.second >= wait_overtime_) {
        it = waited_dgram_.erase(it);
      } else {
        it++;
      }
    }
  }
}
