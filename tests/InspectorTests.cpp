#include <remi/debug/SceneInspector.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main() {
    try {
        remi::Scene scene;
        const auto parent = scene.Create("parent"), child = scene.Create("child");
        Check(scene.SetParent(child,parent),"Parent setup failed");
        remi::debug::SceneInspector inspector;
        remi::Input input;
        input.SetKey(remi::Key::F3,true);
        inspector.Update(scene,input,1.f/60);
        Check(inspector.Visible() && inspector.Selected() == parent,"Inspector did not open on a valid entity");
        input.Reset(); input.SetKey(remi::Key::Down,true);
        inspector.Update(scene,input,1.f/60);
        Check(inspector.Selected() == child,"Inspector selection failed");
        input.Reset(); input.SetKey(remi::Key::Control,true); input.SetKey(remi::Key::Right,true);
        inspector.Update(scene,input,.5f);
        Check(std::abs(scene.Get<remi::TransformComponent>(child)->position.x-.5f) < .0001f &&
              inspector.Selected() == child,"Inspector edit changed selection or wrong transform");
        Check(scene.Destroy(child),"Delete selected entity failed");
        input.Reset(); inspector.Update(scene,input,0);
        Check(inspector.Selected() == parent,"Deleted selection was not recovered");
        scene.Clear(); inspector.Update(scene,input,0);
        Check(inspector.Selected() == remi::EntityId{},"Empty scene retained stale selection");
        input.SetKey(remi::Key::F3,true); inspector.Update(scene,input,0);
        Check(!inspector.Visible(),"Inspector did not release input capture");
        std::cout << "Inspector selection, local edits, stale entity recovery and input capture passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
