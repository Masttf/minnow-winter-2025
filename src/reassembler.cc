#include "reassembler.hh"
#include "debug.hh"

using namespace std;

/*
维护一个map，键为起始位置，值为string
可能重叠、越界、重复、离散
*/
void Reassembler::insert( uint64_t first_index, string data, bool is_last_substring )
{
  if ( data.empty() ) {
    // 特判空串 - 记录最后位置但不直接关闭
    if ( is_last_substring ) {
      has_last_ = true;
      last_index_ = first_index;
      // 空串的最后位置就是起始位置
      // 检查是否所有数据都已到达
      if ( first_unassembled_index_ >= last_index_ ) {
        output_.writer().close();
      }
    }
    return;
  }
  uint64_t end_ = first_unassembled_index_ + available_capacity();
  if ( first_index >= end_ ) {
    return;
  }
  uint64_t L = first_index;
  uint64_t R = L + data.size();
  // 已到达部分截断
  if ( first_index < first_unassembled_index_ ) {
    L = first_unassembled_index_;
  }
  // 过长截断
  if ( R > end_ ) {
    R = end_;
    is_last_substring = false;
  }
  // 空
  if ( L >= R ) {
    return;
  }
  // 插入buffer
  {
    // 找到一个 l <= L 的位置
    auto it = buffer.upper_bound( L );
    if ( it != buffer.begin() ) {
      it--;
    }
    while ( it != buffer.end() ) {
      if ( L >= R ) {
        break;
      }
      uint64_t l_it = it->first;
      uint64_t r_it = l_it + it->second.size();
      // 没有交集
      if ( r_it <= L ) {
        it++;
        continue;
      }
      if ( R <= l_it ) {
        break;
      }

      // data完全包含这段，则删除
      if ( L <= l_it && r_it <= R ) {
        it = buffer.erase( it );
        continue;
      }

      // 左边相交
      if ( l_it <= L && L < r_it ) {
        L = r_it;
      }
      // 右边相交
      if ( l_it <= R && R <= r_it ) {
        R = l_it;
      }
      it++;
    }
    if ( L < R ) {
      buffer[L] = data.substr( L - first_index, R - L );
    }
  }
  if ( is_last_substring ) {
    has_last_ = true;
    last_index_ = first_index + data.size(); // 记录最后一个字节之后的位置
  }
  // wirte
  while ( !buffer.empty() && buffer.begin()->first == first_unassembled_index_ ) {
    first_unassembled_index_ += buffer.begin()->second.size();
    output_.writer().push( std::move( buffer.begin()->second ) );
    buffer.erase( buffer.begin() );
  }

  if ( has_last_ && first_unassembled_index_ >= last_index_ ) {
    output_.writer().close();
  }
}

// How many bytes are stored in the Reassembler itself?
// This function is for testing only; don't add extra state to support it.
uint64_t Reassembler::count_bytes_pending() const
{
  uint64_t ans = 0;
  for ( auto& [x, y] : buffer ) {
    ans += y.size();
  }
  return ans;
}
