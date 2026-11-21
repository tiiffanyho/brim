// EventBus: the only way processes talk to each other.
//
//   brim ──events──▶ app_server        (channel "brim_events")
//   brim ──heartbeat pulses──▶ supervisor   (channel "brim_supervisor")
//   supervisor ──fault events──▶ app_server
//   app_server ──commands──▶ brim      (channel "brim_cmd")
//
// Two backends, chosen at build time (CMake option BRIM_IPC_BACKEND):
//   qnx : name_attach()/name_open() + MsgSendv()/MsgReceive() + MsgSendPulse() (Pi, QNX 8.0)
//   uds : AF_UNIX SOCK_DGRAM sockets in $BRIM_RUN_DIR (laptop dev: macOS/Linux)
// Payloads are JSON text matching shared/events.schema.json. Pulses carry (code, value) only.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace brim::ipc {

struct Message {
  enum class Kind { Event, Pulse };
  Kind kind = Kind::Event;
  int code = 0;       // pulse code (application-relative, i.e. without _PULSE_CODE_MINAVAIL)
  int value = 0;      // pulse value
  std::string payload;  // event JSON
};

// Receiving side of a named channel. Exactly one process owns each channel.
class Endpoint {
 public:
  virtual ~Endpoint() = default;
  // Waits up to timeout_ms for one message. nullopt on timeout or error.
  virtual std::optional<Message> receive(int timeout_ms) = 0;
};

// Sending side. Connects lazily and reconnects after failures (the receiver may restart).
class Connection {
 public:
  virtual ~Connection() = default;
  // Sends one event. Returns false if the receiver is not reachable (message is dropped).
  // UDS: never blocks. QNX: MsgSend with a 100 ms send/reply timeout.
  virtual bool send(std::string_view payload) = 0;
  // Sends a pulse. Never blocks on either backend.
  virtual bool pulse(int code, int value) = 0;
};

std::unique_ptr<Endpoint> listen(const std::string& channel, std::string* err = nullptr);
std::unique_ptr<Connection> connect(const std::string& channel);
const char* backend_name();

// Asynchronous publisher: publish() copies into a bounded queue and returns immediately;
// a background thread delivers. When full, the OLDEST queued event is dropped (fresh data wins).
// Use one EventBus per destination channel.
class EventBus {
 public:
  explicit EventBus(std::string channel, std::size_t capacity = 256);
  ~EventBus();
  EventBus(const EventBus&) = delete;
  EventBus& operator=(const EventBus&) = delete;

  bool publish(std::string json);  // false if something had to be dropped
  bool pulse(int code, int value);  // synchronous but non-blocking

  std::uint64_t sent() const { return sent_.load(); }
  std::uint64_t dropped() const { return dropped_.load(); }
  std::size_t queued() const;

 private:
  void run();

  std::string channel_;
  std::size_t capacity_;
  std::unique_ptr<Connection> conn_;        // used only by the worker thread
  std::unique_ptr<Connection> pulse_conn_;  // separate connection for pulses (caller thread)
  std::mutex pulse_mu_;
  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::deque<std::string> q_;
  bool stop_ = false;
  std::atomic<std::uint64_t> sent_{0};
  std::atomic<std::uint64_t> dropped_{0};
  std::thread worker_;
};

}  // namespace brim::ipc
