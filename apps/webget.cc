#include "tcp_minnow_socket.hh"

#include <cstdlib>
#include <iostream>
#include <span>
#include <string>

using namespace std;

void get_URL( const string& host, const string& path )
{
  Address addr = Address( host, "http" );
  CS144TCPSocket tcp;
  tcp.connect( addr );

  // 构建完整的 HTTP 请求
  string request = "GET " + path + " HTTP/1.1\r\n";
  request += "Host: " + host + "\r\n";
  request += "Connection: close\r\n";
  request += "\r\n"; // 空行表示请求头结束

  // 发送请求
  tcp.write( request );

  // 服务器响应后会把eof置为true，这样客户端就知道响应了
  // 读取响应直到 EOF
  std::cout << "write" << endl;
  while ( !tcp.eof() ) {
    // std::cout << 1 << endl;
    string buffer;
    tcp.read( buffer );
    cout << buffer;
  }
  tcp.wait_until_closed();
}

int main( int argc, char* argv[] )
{
  try {
    if ( argc <= 0 ) {
      abort(); // For sticklers: don't try to access argv[0] if argc <= 0.
    }

    auto args = span( argv, argc );

    // The program takes two command-line arguments: the hostname and "path" part of the URL.
    // Print the usage message unless there are these two arguments (plus the program name
    // itself, so arg count = 3 in total).
    if ( argc != 3 ) {
      cerr << "Usage: " << args.front() << " HOST PATH\n";
      cerr << "\tExample: " << args.front() << " stanford.edu /class/cs144\n";
      return EXIT_FAILURE;
    }

    // Get the command-line arguments.
    const string host { args[1] };
    const string path { args[2] };

    // Call the student-written function.
    get_URL( host, path );
  } catch ( const exception& e ) {
    cerr << e.what() << "\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
