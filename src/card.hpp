#pragma once

#include <string>

namespace mfmx {

  class Card {
  public:
    // Constructor
    Card(int value);

    // Member function
    std::string getDescription() const;

  private:
    int m_value;
  };

} // namespace mfmx
