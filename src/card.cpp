#include "card.hpp"
#include <string>

namespace mfmx {

  Card::Card(int value) : m_value(value) {}

  std::string Card::getDescription() const {
    return "This is a Card with value " + std::to_string(m_value) + ".";
  }

} // namespace mfmx
