#include "BotState.hpp"

namespace acgdl {

    BotStats& stats() {
        static BotStats s;
        return s;
    }

}
