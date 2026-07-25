The Dragon Pixel Engine is a Game Engine and Tooling for Existing Engines such as Unity, And Unreal Engine but mainly focused as a Engine Driving Monogame and KNI frameworks that can be leveraged for the standalone editor there will be an editor portion that will be a lot like unity with a render panel area and and an inspector view where users can add components that will be able to be worked with I want parody to unity initially. I want the standalone editor to be coded in C++ and have the scripting language being able to use C# or C++. Ideally I would like to add more agnostic behaviors The latest C# 14 and .NET 10 will be used for the Monogame/KNI editor and for Unity, .net standard 2.1 I believe can be used. so shared libraries should match compatibility across the board for the engine. I want this to be able to also take existing projects in Monogame/KNI and migrate them to the engine and be able to have visual editor in the view.

the design I want to have is interface first, Organize the code with interfaces wherever possible and similar the c++ engine should feel like a proper editor. Components are C# files or C++ code 

the idea is each project will be setup and assembled let's do this in small chunks not all at once. get the architecture and design. also there needs to be a pipeline with python for AI workflows

The Core Engine uses an entity interface structure.

each entity will have a GUID ID, and a string Name nearly everything will inherit a class or other interfaces. I want the architecture to be modular component based design even for the engine.

there will be a explorer, inspector, scene view, and console/logging, ways to play to game and test similar to unity.

I want to break this into 4 slices to start off with. Core/Infrastructure/Scaffolding, and Core editor, Then 2nd slice is tooling and tailoring for designer friendly UI and Design. 3nd slice is is project creation, edit, delete, updater and details that are relevant for core workflow. and 4th slice is polish and core guts to get to version 1.0.0 each iteration being the goal to launch a stable project flow. I want this to be as close to Unity as possible when it comes to idea how how a editor should look.


I want to make all of the editor UI functional and working I should be able to add gameobjects, there should be a project explorer, all for Slice 1 should be working let's get the slice built into working fully functional editor.





I want to mirror the documentation markdown files in the repo so it is in-sync with the design document and LLM prompt source, this directory C:\Projects\Documentation\Engines\Dragon Pixel Engine can be used for documentation and mirrored inside the docs folder in the repo so all these markdowns are stored in the repo and in my document system. add to agents file that all documents and plans and responses will be added as expected.




Let's start building the Editor i'm on a windows machine so let's get windows dev environment setup for C++ coding and able to easily test changes. let;s start with Phase 1


Let's get the environment setup for Slice 1 (Phase 1), I also want to make sure to have you capture all plans and place them in both the docs and the C:\Projects\Documentation\Engines\Dragon Pixel Engine\Plans path.


Let's go ahead and get Slice 1: Core, infrastructure, scaffolding, and core editor done entirely.
