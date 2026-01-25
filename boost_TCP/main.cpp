#include <boost/asio.hpp>
#include <iostream>
#include <memory>

using boost::asio::ip::tcp;

class Session : public std::enable_shared_from_this<Session> {
public:
  Session(tcp::socket socket) : socket_(std::move(socket)) {}

  void start() {
    read();
  }

private:
  void read() {
    auto self = shared_from_this();
    socket_.async_read_some(
        boost::asio::buffer(data_),
        [this, self](boost::system::error_code ec, std::size_t length) {
            if (!ec) {
                std::string msg(data_, length);
                std::cout << "Received: " << msg << "\n";
                write(length);
            }
        }
    );
  }

  void write(std::size_t length) {
    auto self = shared_from_this();
    boost::asio::async_write(
        socket_, boost::asio::buffer(data_, length),
        [this, self](boost::system::error_code ec, std::size_t /*length*/) {
            if (!ec) {
                read();  // Continue reading next messages (non-blocking)
            }
        }
    );
  }

  tcp::socket socket_;
  char data_[1024];
};

class Server {
public:
  Server(boost::asio::io_context& io, short port)
      : acceptor_(io, tcp::endpoint(tcp::v4(), port)) {
    accept();
  }

private:
  void accept() {
    acceptor_.async_accept(
        [this](boost::system::error_code ec, tcp::socket socket) {
            if (!ec) {
                std::cout << "New client connected\n";
                std::make_shared<Session>(std::move(socket))->start();
            }
            accept();  // keep accepting new clients (non-blocking)
        }
    );
  }

  tcp::acceptor acceptor_;
};

int main() {
  try {
    boost::asio::io_context io;
    Server server(io, 9000);
    io.run();
  } catch (std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
  }
}
