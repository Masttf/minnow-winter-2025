#include "tcp_sender.hh"
#include "debug.hh"
#include "tcp_config.hh"

using namespace std;

// This function is for testing only; don't add extra state to support it.
uint64_t TCPSender::sequence_numbers_in_flight() const
{
  uint64_t ans = 0;
  for (auto&x : outstanding_) {
    ans += x.sequence_length();
  }
  return ans;
}

// This function is for testing only; don't add extra state to support it.
uint64_t TCPSender::consecutive_retransmissions() const
{
  return count_;
}

void TCPSender::push( const TransmitFunction& transmit )
{

  if (reader().has_error()) {
    TCPSenderMessage msg;
    msg.seqno = Wrap32::wrap(next_abs_seqno, isn_);
    msg.RST = true;
    transmit(msg);
    // RST 不需要收到确认和计时
    return; 
  }

  uint64_t flight_size = next_abs_seqno - rec_ackno;
  uint64_t effective_window = window_size_ > flight_size ? window_size_ - flight_size : 0;

  // 处理零窗口探测
  if (window_size_ == 0 && outstanding_.empty()) {
      effective_window = 1; 
  }

  while(effective_window > 0) {
    TCPSenderMessage msg {};
    msg.seqno = Wrap32::wrap(next_abs_seqno, isn_);
    // 处理SYN
    if (!syn_sent_) {
      syn_sent_ = true;
      msg.SYN = true;
    }

    // payload
    string data = "";
    uint64_t len = min(effective_window - msg.SYN, static_cast<uint64_t>(TCPConfig::MAX_PAYLOAD_SIZE));
    read(reader(), len, data);
    msg.payload = data;

    // 处理FIN
    if (reader().is_finished() && effective_window > msg.sequence_length() && !fin_sent_) {
      msg.FIN = true;
      fin_sent_ = true;
    }

    // 空串
    if (msg.sequence_length() == 0) {
      break;
    }

    next_abs_seqno += msg.sequence_length();
    effective_window -= msg.sequence_length();
    // 只要发送数据包就启动
    if (timer_running == false) {
      timer_running = true;
      elapsed_ms_ = 0;
      cur_RTO_ms_ = initial_RTO_ms_;
    }
    transmit(msg);
    outstanding_.push_back(std::move(msg));
  };
}

TCPSenderMessage TCPSender::make_empty_message() const
{
  TCPSenderMessage res {};
  res.seqno = Wrap32::wrap(next_abs_seqno, isn_);
  if (reader().has_error()) {
    res.RST = true;
  }
  return res;
}

void TCPSender::receive( const TCPReceiverMessage& msg )
{
  if (msg.RST) {
    reader().set_error();
    return ;
  }
  window_size_ =  msg.window_size;
  if (msg.ackno.has_value()) {
    uint64_t abs_ackno = msg.ackno.value().unwrap(isn_, next_abs_seqno);
    // 超过忽略
    if (abs_ackno > next_abs_seqno || abs_ackno <= rec_ackno) {
      return ;
    }
    rec_ackno = abs_ackno;
    while(!outstanding_.empty()) {
      uint64_t seg_start = outstanding_.front().seqno.unwrap(isn_, next_abs_seqno);
      uint64_t seg_end = seg_start + outstanding_.front().sequence_length();
      if (seg_end <= abs_ackno) {
        outstanding_.pop_front();
      } else {
        break;
      }
    }
    // 只要收到了新的确认号
    cur_RTO_ms_ = initial_RTO_ms_;
    count_ = 0;
    if (outstanding_.empty()) {
      timer_running = false;
    } else {
      timer_running = true;
      elapsed_ms_ = 0;
    }
  }
  return ;
}

void TCPSender::tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit )
{
  if (!timer_running) {
    return ;
  }
  elapsed_ms_ += ms_since_last_tick;
  if (elapsed_ms_ >= cur_RTO_ms_) {
    transmit(outstanding_.front());
    elapsed_ms_ = 0;
    // 只有在窗口大小非零时，才执行指数退避！
    // 窗口为0说明网络状况完全无关所以无需指数退避
    if (window_size_ > 0) {
        cur_RTO_ms_ *= 2;
    }
    count_++;
  }
  return ;
}
