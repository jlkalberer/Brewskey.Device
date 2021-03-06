#pragma once

#include "Tappt/Tap/Tap.h"
#include "Tappt/KegeratorStateMachine/IKegeratorStateMachine.h"

TEST_CASE("Tap::GetId", "[Tap]") {
  fakeit::Mock<IKegeratorStateMachine> mock;
  SECTION("tap ID is empty") {
    Tap tap;
    REQUIRE(tap.GetId() == 0);
  }

  SECTION("tap ID is equal") {
    Tap tap;
    tap.Setup(&mock.get(), 123, 0, 5);
    REQUIRE(tap.GetId() == 123);
  }
}
