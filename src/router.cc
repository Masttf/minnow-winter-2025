#include "router.hh"
#include "debug.hh"

#include <iostream>

using namespace std;

// route_prefix: The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
// prefix_length: For this route to be applicable, how many high-order (most-significant) bits of
//    the route_prefix will need to match the corresponding bits of the datagram's destination address?
// next_hop: The IP address of the next hop. Will be empty if the network is directly attached to the router (in
//    which case, the next hop address should be the datagram's final destination).
// interface_num: The index of the interface to send the datagram out on.
void Router::add_route( const uint32_t route_prefix,
                        const uint8_t prefix_length,
                        const optional<Address> next_hop,
                        const size_t interface_num )
{
  cerr << "DEBUG: adding route " << Address::from_ipv4_numeric( route_prefix ).ip() << "/"
       << static_cast<int>( prefix_length ) << " => " << ( next_hop.has_value() ? next_hop->ip() : "(direct)" )
       << " on interface " << interface_num << "\n";

  router_table_.push_back(RouterTableItem{route_prefix, prefix_length, next_hop, interface_num});
}

// Go through all the interfaces, and route every incoming datagram to its proper outgoing interface.
void Router::route()
{
  for (auto x: interfaces_) {
    auto& que = x->datagrams_received();
    while(!que.empty()) {
      InternetDatagram dgram = que.front();
      que.pop();
      if(dgram.header.ttl == 0) {
        continue;
      }
      
      // ttl减1 如果到0在实际网络中应该会发送ICMP包
      dgram.header.ttl--;
      if(dgram.header.ttl == 0) {
        continue;
      }
      
      // 重新计算校验和
      dgram.header.compute_checksum();
      
      size_t res = interfaces_.size();
      uint8_t max_prefix_length_ = 0;
      RouterTableItem ans;
      
      for (auto &y : router_table_) {
        uint32_t mask;
        if (y.prefix_length == 0) {
          mask = 0;
        } else if (y.prefix_length >= 32) {
          mask = 0xFFFFFFFF; 
        } else {
          mask = ~((1ULL << (32 - y.prefix_length)) - 1);
        }
        
        if ((dgram.header.dst & mask) == (y.route_prefix & mask)) {
          if (y.prefix_length >= max_prefix_length_) {
            max_prefix_length_ = y.prefix_length;
            res = y.interface_num;
            ans = y;
          }
        }
      }
      
      // 未匹配
      if (res == interfaces_.size()) {
        continue;
      } else {
        if (ans.next_hop.has_value()) {
          // 发送到下一条
          interface(res)->send_datagram(dgram, ans.next_hop.value());
        } else {
          // 就是本网络
          Address dest_addr = Address::from_ipv4_numeric(dgram.header.dst);
          interface(res)->send_datagram(dgram, dest_addr);
        }
      }
    }
  }
}
