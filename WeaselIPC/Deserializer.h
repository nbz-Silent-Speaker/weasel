#pragma once
#include <ResponseParser.h>
#include <functional>
#include <utility>

namespace weasel {

template <typename T>
bool TryDeserialize(boost::archive::text_wiarchive& ia, T& t) {
  T decoded = t;
  try {
    ia >> decoded;
    t = std::move(decoded);
    return true;
  } catch (const boost::archive::archive_exception& e) {
    const std::string msg =
        std::string("IPC deserialization failed: ") + e.what() + "\n";
    ::OutputDebugStringA(msg.c_str());
    return false;
  }
}
class Deserializer {
 public:
  typedef std::vector<std::wstring> KeyType;
  typedef std::shared_ptr<Deserializer> Ptr;
  typedef std::function<Ptr(ResponseParser* pTarget)> Factory;

  Deserializer(ResponseParser* pTarget) : m_pTarget(pTarget) {}
  virtual ~Deserializer() {}
  virtual void Store(KeyType const& key, std::wstring const& value) {}

  static void Initialize(ResponseParser* pTarget);
  static void Define(std::wstring const& action, Factory factory);
  static bool Require(std::wstring const& action, ResponseParser* pTarget);

 protected:
  ResponseParser* m_pTarget;

 private:
  static std::map<std::wstring, Factory> s_factories;
};

}  // namespace weasel
