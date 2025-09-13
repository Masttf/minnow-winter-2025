#include "tcp_receiver.hh"
#include "debug.hh"

using namespace std;

void TCPReceiver::receive( TCPSenderMessage message )
{
  // RST 标志应该总是被处理，无论连接状态如何
  if ( message.RST ) {
    reader().set_error();
    return;
  }

  if ( !isn_.has_value() ) {
    if ( !message.SYN ) {
      return;
    }
    isn_ = message.seqno;
  }

  const uint64_t checkpoint = writer().bytes_pushed() + 1;
  const uint64_t abo_seqno = message.seqno.unwrap( isn_.value(), checkpoint );
  const uint64_t stream_index = message.SYN ? 0 : abo_seqno - 1;
  reassembler_.insert( stream_index, message.payload, message.FIN );
}

TCPReceiverMessage TCPReceiver::send() const
{
  TCPReceiverMessage res {};
  if ( isn_.has_value() ) {
    uint64_t offset = 1;
    // FIN 也占一个
    if ( writer().is_closed() ) {
      offset = 2;
    }
    res.ackno = Wrap32::wrap( writer().bytes_pushed() + offset, isn_.value() );
  }
  res.window_size = std::min( writer().available_capacity(), static_cast<uint64_t>( UINT16_MAX ) );
  if ( writer().has_error() ) {
    res.RST = true;
  }
  return res;
}
