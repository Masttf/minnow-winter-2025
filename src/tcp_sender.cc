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
  uint64_t ws = 0;
  if (window_size_ == 0 && outstanding_.empty()) {
    ws = 1;
  }
  if (rev_ackno + window_size_ > next_abs_seqno) {
    ws = max(ws, rev_ackno + window_size_ - next_abs_seqno);
  }
  TCPSenderMessage msg {};
  if (!syn_sent_) {
    syn_sent_ = true;
    msg.SYN = true;
    msg.seqno = isn_;
    next_abs_seqno++;
    ws--;
  }
  do {
    //判断seqno填过没
    if (msg.SYN == false) {
      msg.seqno = Wrap32::wrap(next_abs_seqno, isn_);
    }

    if (reader().has_error()) {
      msg.RST = true;
    }
    string data = "";
    uint64_t len = min(ws, static_cast<uint64_t>(TCPConfig::MAX_PAYLOAD_SIZE));
    while(data.size() < len) {
      if (reader().is_finished() || reader().bytes_buffered() == 0) {
        break;
      }
      string_view peek = reader().peek();
      if (data.size() + peek.size() > len) {
        peek.remove_suffix(peek.size() + data.size() - len);
      }
      data += peek;
      reader().pop(peek.size());
    }
    ws -= data.size();
    next_abs_seqno += data.size();
    msg.payload = data;

    // 判断能不能放下FIN
    if (reader().is_finished() && ws > 0 && !fin_sent_) {
      debug("FIN");
      msg.FIN = true;
      fin_sent_ = true;
      ws--;
      next_abs_seqno++;
    }

    // 空串
    if (msg.sequence_length() == 0) {
      break;
    }

    // 只要发送数据包就启动
    if (timer_running == false) {
      timer_running = true;
      elapsed_ms_ = 0;
      cur_RTO_ms_ = initial_RTO_ms_;
    }
    transmit(msg);
    outstanding_.push_back(std::move(msg));
    msg = {};
  } while(ws > 0);
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
    if (abs_ackno > next_abs_seqno) {
      return ;
    }
    rev_ackno = max(rev_ackno, abs_ackno);
    while(!outstanding_.empty()) {
      uint64_t seg_start = outstanding_.front().seqno.unwrap(isn_, next_abs_seqno);
      uint64_t seg_end = seg_start + outstanding_.front().sequence_length();
      if (seg_end <= abs_ackno) {
        outstanding_.pop_front();
        cur_RTO_ms_ = initial_RTO_ms_;
        count_ = 0;
        timer_running = false;
        // 有任何未完成的数据，重新启动重传计时器
        if (!outstanding_.empty()) {
          timer_running = true;
          elapsed_ms_ = 0;
        }
      } else {
        break;
      }
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
    if (window_size_ > 0) {
        cur_RTO_ms_ *= 2;
    }
    count_++;
  }
  return ;
}
