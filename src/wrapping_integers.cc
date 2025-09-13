#include "wrapping_integers.hh"
#include "debug.hh"

using namespace std;

Wrap32 Wrap32::wrap( uint64_t n, Wrap32 zero_point )
{
  return zero_point + static_cast<uint32_t>( n );
}

uint64_t Wrap32::unwrap( Wrap32 zero_point, uint64_t checkpoint ) const
{
  static constexpr uint64_t TWO31 = 1UL << 31;
  static constexpr uint64_t TWO32 = 1UL << 32;

  const Wrap32 chk = wrap( checkpoint, zero_point );
  // 拿到相对差值
  const uint32_t dis = raw_value_ - chk.raw_value_;

  // 左边更近
  if ( dis <= TWO31 || checkpoint + dis < TWO32 ) {
    return checkpoint + dis;
  }
  return checkpoint + dis - TWO32;
}
