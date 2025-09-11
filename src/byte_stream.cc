#include "byte_stream.hh"

using namespace std;

ByteStream::ByteStream( uint64_t capacity ) : capacity_( capacity ) {}

void Writer::push( string data )
{
  uint64_t n = std::min( available_capacity(), static_cast<uint64_t>( data.size() ) );
  if ( n == 0 ) {
    return;
  }
  data = data.substr( 0, n );
  buffer.push_back( data );
  len_ += n;
  num_pushed_ += n;
  if ( buffer.size() == 1 ) {
    front_ = buffer.front();
  }
}

void Writer::close()
{
  is_closed_ = true;
}

bool Writer::is_closed() const
{
  return is_closed_;
}

uint64_t Writer::available_capacity() const
{
  return capacity_ - len_;
}

uint64_t Writer::bytes_pushed() const
{
  return num_pushed_;
}

string_view Reader::peek() const
{
  return front_;
}

void Reader::pop( uint64_t len )
{
  len = min( len, len_ );
  while ( len > 0 ) {
    uint64_t n = min( static_cast<uint64_t>( front_.size() ), len );
    front_.remove_prefix( n );
    if ( front_.empty() ) {
      buffer.pop_front();
      if ( !buffer.empty() ) {
        front_ = buffer.front();
      } else {
        front_ = {};
      }
    }
    len -= n;
    len_ -= n;
    num_poped_ += n;
  }
}

bool Reader::is_finished() const
{
  return is_closed_ && ( len_ == 0 );
}

uint64_t Reader::bytes_buffered() const
{
  return len_;
}

uint64_t Reader::bytes_popped() const
{
  return num_poped_;
}
