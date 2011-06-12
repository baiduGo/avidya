/*
 * Copyright(C) lichuang
 */
#include <map>
#include <list>
#include <string>
#include "eventrpc/error_code.h"
#include "eventrpc/log.h"
#include "eventrpc/event.h"
#include "eventrpc/callback.h"
#include "eventrpc/dispatcher.h"
#include "eventrpc/net_utility.h"
#include "eventrpc/message_connection.h"
#include "eventrpc/buffer.h"
using namespace std;
namespace eventrpc {
struct MessageConnectionEvent : public Event {
  MessageConnectionEvent(int fd, MessageConnection::Impl *impl)
    : Event(fd, EVENT_READ | EVENT_WRITE),
      impl_(impl) {
  }

  virtual ~MessageConnectionEvent() {
  }

  int HandleRead();

  int HandleWrite();

  MessageConnection::Impl *impl_;
};

struct MessageConnection::Impl {
  Impl(MessageConnection *connection, MessageConnectionManager *manager);

  ~Impl();

  void set_fd(int fd);

  void set_client_address(struct sockaddr_in address);

  void set_dispacher(Dispatcher *dispatcher);

  void set_message_handler(MessageHandler *handler);

  Event* event();

  void SendPacket(uint32 opcode, const ::google::protobuf::Message *message);

  void Close();

  int HandleRead();

  int HandleWrite();

  void ErrorMessage(const string &message);
 private:
  MessageConnection *connection_;
  MessageConnectionEvent event_;
  struct sockaddr_in client_address_;
  MessageConnectionManager *connection_manager_;
  Dispatcher *dispatcher_;
  Buffer input_buffer_;
  Buffer output_buffer_;
  MessageHandler *handler_;
  MessageHeader message_header_;
  ReadMessageState state_;
};

MessageConnection::Impl::Impl(MessageConnection *connection,
                              MessageConnectionManager *manager)
  : connection_(connection),
    event_(-1, this),
    connection_manager_(manager),
    handler_(NULL),
    state_(READ_HEADER) {
}

MessageConnection::Impl::~Impl() {
  Close();
}

void MessageConnection::Impl::ErrorMessage(const string &message) {
  char buffer[30];
  VLOG_ERROR() << message
    << inet_ntop(AF_INET, &client_address_.sin_addr,
                 buffer, sizeof(buffer))
    << ":" << ntohs(client_address_.sin_port) << " error";
  Close();
}

int MessageConnection::Impl::HandleRead() {
  if (input_buffer_.Read(event_.fd_) == -1) {
    ErrorMessage("recv from ");
    return -1;
  }
  while (!input_buffer_.is_read_complete()) {
    VLOG_INFO() << "HandleRead, size: " << input_buffer_.end_position();
    uint32 result = ReadMessageStateMachine(&input_buffer_,
                                            &message_header_,
                                            &state_);
    if (result == kRecvMessageNotCompleted) {
      return result;
    }
    if (!handler_->HandlePacket(message_header_, &input_buffer_)) {
    ErrorMessage("handle message from ");
    return result;
    }
  }
  input_buffer_.Clear();
  return kSuccess;
}

int MessageConnection::Impl::HandleWrite() {
  if (output_buffer_.is_read_complete()) {
    return kSuccess;
  }
  uint32 result = WriteMessage(&output_buffer_, event_.fd_);
  if (result == kSendMessageError) {
    ErrorMessage("send message to ");
  }
  return result;
}

void MessageConnection::Impl::set_fd(int fd) {
  event_.fd_ = fd;
}

void MessageConnection::Impl::set_client_address(struct sockaddr_in address) {
  client_address_ = address;
}

void MessageConnection::Impl::set_dispacher(Dispatcher *dispatcher) {
  dispatcher_ = dispatcher;
}

void MessageConnection::Impl::set_message_handler(MessageHandler *handler) {
  handler_ = handler;
}

void MessageConnection::Impl::SendPacket(
    uint32 opcode, const ::google::protobuf::Message *message) {
  EncodePacket(opcode, message, &output_buffer_);
  uint32 result = WriteMessage(&output_buffer_, event_.fd_);
  if (result == kSendMessageError) {
    ErrorMessage("send message to ");
  }
}

Event* MessageConnection::Impl::event() {
  return &event_;
}

void MessageConnection::Impl::Close() {
  if (event_.fd_ > 0) {
    dispatcher_->DeleteEvent(&event_);
    connection_manager_->PutConnection(connection_);
    input_buffer_.Clear();
    output_buffer_.Clear();
    state_ = READ_HEADER;
  }
}

MessageConnection::MessageConnection(MessageConnectionManager *manager) {
  impl_ = new Impl(this, manager);
}

MessageConnection::~MessageConnection() {
  delete impl_;
}

void MessageConnection::set_fd(int fd) {
  impl_->set_fd(fd);
}

void MessageConnection::set_client_address(struct sockaddr_in address) {
  impl_->set_client_address(address);
}

void MessageConnection::set_dispacher(Dispatcher *dispatcher) {
  impl_->set_dispacher(dispatcher);
}

void MessageConnection::set_message_handler(MessageHandler *handler) {
  impl_->set_message_handler(handler);
}

int MessageConnectionEvent::HandleRead() {
  return impl_->HandleRead();
}

int MessageConnectionEvent::HandleWrite() {
  return impl_->HandleWrite();
}

void MessageConnection::SendPacket(
    uint32 opcode, const ::google::protobuf::Message *message) {
  impl_->SendPacket(opcode, message);
}

Event* MessageConnection::event() {
  return impl_->event();
}

void MessageConnection::Close() {
  impl_->Close();
}
};