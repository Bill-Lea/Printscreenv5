#include "PCH.h"
#include "CancellationToken.h"

void CancellationToken::ThrowIfCancelled(const char* stage) const {
    if (IsCancelled()) {
        logger::info("Cancellation requested at stage: {}", stage);
        throw Cancelled{};
    }
}
