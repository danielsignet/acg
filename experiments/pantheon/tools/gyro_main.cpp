#include "GyroscopeAnimation.h"
#include "RoomIsolation.h"
#include <vr/Viewer.h>
#include <algorithm>
#include <string>
#include <vector>

int main(int argc, char** argv){
    pantheon::registerGyroscopes();
    pantheon::registerRoomIsolation();
    std::vector<std::string> arguments(argv,argv+argc);
    if(std::find(arguments.begin(),arguments.end(),"--scene")==arguments.end()){
        arguments.push_back("--scene");
        arguments.push_back("experiments/pantheon/scene.xml");
    }
    std::vector<char*> values;
    for(auto& argument:arguments)values.push_back(argument.data());
    return vr::Viewer().run(static_cast<int>(values.size()),values.data());
}
