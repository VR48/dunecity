#ifndef PLACEMENT_RESULT_H
#define PLACEMENT_RESULT_H
namespace PlacementResult {
// Tile structures consume their completed queue entry without returning an
// object. Rejected placement leaves the entry in place. Snapshot before the
// call so another queued road cannot be mistaken for the original item.
template<class QueueSize, class Place>
bool apply(QueueSize queueSize, Place place) {
    const auto before=queueSize();
    const bool objectCreated=place();
    return objectCreated || queueSize()<before;
}
}
#endif
