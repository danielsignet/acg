#include "GyroscopeAnimation.h"
#include "RoomIsolation.h"
#include <vr/Viewer.h>

int main(int argc, char** argv){
    pantheon::registerGyroscopes();
    pantheon::registerRoomIsolation();
    return vr::Viewer().run(argc, argv);
}
