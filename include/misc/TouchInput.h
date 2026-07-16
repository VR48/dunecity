#ifndef TOUCHINPUT_H
#define TOUCHINPUT_H

#include <SDL.h>

namespace TouchInput {

bool translateTouchEvent(SDL_Event& event);

}

#endif // TOUCHINPUT_H
