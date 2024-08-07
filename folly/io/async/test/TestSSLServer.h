/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <fcntl.h>
#include <sys/types.h>

#include <list>

#include <folly/SocketAddress.h>
#include <folly/io/async/AsyncSSLSocket.h>
#include <folly/io/async/AsyncServerSocket.h>
#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/AsyncTimeout.h>
#include <folly/io/async/AsyncTransport.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/ssl/SSLErrors.h>
#include <folly/io/async/test/CallbackStateEnum.h>
#include <folly/portability/GTest.h>
#include <folly/portability/Sockets.h>
#include <folly/portability/Unistd.h>
#include <folly/testing/TestUtil.h>

namespace folly::test {

extern const char* kTestCert;
extern const char* kTestKey;
extern const char* kTestCA;
extern const char* kTestCertCN;

extern const char* kClientTestCert;
extern const char* kClientTestKey;
extern const char* kClientTestCA;
extern const char* kClientTestChain;

class HandshakeCallback;

class SSLServerAcceptCallbackBase : public AsyncServerSocket::AcceptCallback {
 public:
  explicit SSLServerAcceptCallbackBase(HandshakeCallback* hcb)
      : state(STATE_WAITING), hcb_(hcb) {}

  ~SSLServerAcceptCallbackBase() override { EXPECT_EQ(STATE_SUCCEEDED, state); }

  void acceptError(folly::exception_wrapper ex) noexcept override {
    LOG(WARNING) << "SSLServerAcceptCallbackBase::acceptError " << ex;
    state = STATE_FAILED;
  }

  void connectionAccepted(
      folly::NetworkSocket fd,
      const SocketAddress& clientAddr,
      AcceptInfo /* info */) noexcept override {
    if (socket_) {
      socket_->detachEventBase();
    }
    LOG(INFO) << "Connection accepted";
    try {
      // Create a AsyncSSLSocket object with the fd. The socket should be
      // added to the event base and in the state of accepting SSL connection.
      socket_ = AsyncSSLSocket::newSocket(
          ctx_,
          base_,
          fd,
          /*server=*/true,
          /*deferSecurityNegotiation=*/false,
          &clientAddr);
    } catch (const std::exception& e) {
      LOG(ERROR) << "Exception %s caught while creating a AsyncSSLSocket "
                    "object with socket "
                 << e.what() << fd;
      folly::netops::close(fd);
      acceptError(e);
      return;
    }

    connAccepted(socket_);
  }

  virtual void connAccepted(const std::shared_ptr<AsyncSSLSocket>& s) = 0;

  void detach() {
    if (socket_) {
      socket_->detachEventBase();
    }
  }

  StateEnum state;
  HandshakeCallback* hcb_;
  std::shared_ptr<SSLContext> ctx_;
  std::shared_ptr<AsyncSSLSocket> socket_;
  EventBase* base_;
};

class TestSSLServer {
 public:
  static std::unique_ptr<SSLContext> getDefaultSSLContext();

  // Create a TestSSLServer.
  // This immediately starts listening on the given port.
  explicit TestSSLServer(
      SSLServerAcceptCallbackBase* acb, bool enableTFO = false);
  explicit TestSSLServer(
      SSLServerAcceptCallbackBase* acb,
      std::shared_ptr<SSLContext> ctx,
      bool enableTFO = false);

  // Kills the thread.
  virtual ~TestSSLServer();

  EventBase& getEventBase() { return evb_; }

  void loadTestCerts();

  const SocketAddress& getAddress() const { return address_; }

 protected:
  EventBase evb_;
  std::shared_ptr<SSLContext> ctx_;
  SSLServerAcceptCallbackBase* acb_;
  std::shared_ptr<AsyncServerSocket> socket_;
  SocketAddress address_;
  std::thread thread_;

 private:
  void init(bool);
};
} // namespace folly::test
