The Dragon Pixel Engine is a Game Engine and Tooling for Existing Engines such as Unity, And Unreal Engine but mainly focused as a Engine Driving Monogame and KNI frameworks that can be leveraged for the standalone editor there will be an editor portion that will be a lot like unity with a render panel area and and an inspector view where users can add components that will be able to be worked with I want parody to unity initially. I want the standalone editor to be coded in C++ and have the scripting language being able to use C# or C++. Ideally I would like to add more agnostic behaviors The latest C# 14 and .NET 10 will be used for the Monogame/KNI editor and for Unity, .net standard 2.1 I believe can be used. so shared libraries should match compatibility across the board for the engine. I want this to be able to also take existing projects in Monogame/KNI and migrate them to the engine and be able to have visual editor in the view.

the design I want to have is interface first, Organize the code with interfaces wherever possible and similar the c++ engine should feel like a proper editor. Components are C# files or C++ code 

the idea is each project will be setup and assembled let's do this in small chunks not all at once. get the architecture and design. also there needs to be a pipeline with python for AI workflows

The Core Engine uses an entity interface structure.

each entity will have a GUID ID, and a string Name nearly everything will inherit a class or other interfaces. I want the architecture to be modular component based design even for the engine.

there will be a explorer, inspector, scene view, and console/logging, ways to play to game and test similar to unity.

I want to break this into 4 slices to start off with. Core/Infrastructure/Scaffolding, and Core editor, Then 2nd slice is tooling and tailoring for designer friendly UI and Design. 3nd slice is is project creation, edit, delete, updater and details that are relevant for core workflow. and 4th slice is polish and core guts to get to version 1.0.0 each iteration being the goal to launch a stable project flow. I want this to be as close to Unity as possible when it comes to idea how how a editor should look.

I want to make all of the editor UI functional and working I should be able to add gameobjects, there should be a project explorer, all for Slice 1 should be working let's get the slice built into working fully functional editor.

I want to make sure that the panels can be placed and positioned with a fluid grid connection. right now the center portion of the app I cannot position the windows properly. Let's get the editor fully functional. also I would like to add more components like scripts that can be attached to gameobjects and should have a lifespan and such there needs to be abstraction so that users can create objects and manipulate the scene view. Let's get the anchoring and layout fixed




I want to mirror the documentation markdown files in the repo so it is in-sync with the design document and LLM prompt source, this directory C:\Projects\Documentation\Engines\Dragon Pixel Engine can be used for documentation and mirrored inside the docs folder in the repo so all these markdowns are stored in the repo and in my document system. add to agents file that all documents and plans and responses will be added as expected.




Let's start building the Editor i'm on a windows machine so let's get windows dev environment setup for C++ coding and able to easily test changes. let;s start with Phase 1


Let's get the environment setup for Slice 1 (Phase 1), I also want to make sure to have you capture all plans and place them in both the docs and the C:\Projects\Documentation\Engines\Dragon Pixel Engine\Plans path.


Let's go ahead and get Slice 1: Core, infrastructure, scaffolding, and core editor done entirely.


Let's go ahead and get the inspector displaying fields not json to modify but transform and other components. I want the inspector to be as close to Unity's inspector as I can get but more powerful ideally be able to show objects like interfaces and other objects that can be configured. let's also get a tile system and a scene and game view


## 2026-07-25 Version 1.0 Goal Expansion

Use `C:\Projects\Documentation\Engines\Dragon Pixel Engine` for the living documents, follow the repository `AGENTS.md`, and expand the active goal to finish Slices 1 through 4 with the complete, fully functional Dragon Pixel Engine 1.0 feature set defined by the Design Document.

## 2026-07-26 Production Editor Iteration Request

Compile the editor and generate a directly runnable Windows executable. Provide a script that builds a production-style editor output and runs it, with a fast incremental path for repeatedly testing editor changes.

## 2026-07-26 Managed GameObject Controller Request

Structure managed C# scripts around a `GameObjectController` base class and lifespan interfaces with `Enabled`, `Disabled`, `Update`, and `FixedUpdate`. Give every controller a stable GUID and a `Vector3`-based Transform, hide lifecycle implementation details from Inspector, and make `MyMover` move its attached GameObject with WASD and arrow keys.

Make `MyMover` consume the same input actions as `Input Motion 2D`. Add a simple project input system that can capture keyboard, mouse, and gamepad input, organize actions into control maps, and persist rebindings without hard-coding device controls in scripts.

## 2026-07-26 Inspector and Input Settings Follow-up

Make checked GameObject and component states substantially more visible in the Inspector. Verify that project scripts execute correctly, make `MyMover` expose and consume the same configurable horizontal action, vertical action, and speed setup as `Input Motion 2D`, and provide an obvious input-settings entry for configuring the project's keyboard, mouse, and gamepad action handling.

## 2026-07-27 New Project and Authoring Workflow Request

Implement the approved New Project, asset workflow, prefab, Hierarchy, and multi-Inspector plan. A no-project launch should present a Project Hub; authors should create minimal 2D or 3D projects and clean scenes, add primitives and linked prefabs through visible drag/drop workflows, manage copied or linked project assets in a two-pane Project Browser with recoverable removal, use ordered multi-selection and multi-object Hierarchy operations, and create multiple independently lockable Inspector docks that safely edit shared components through one transaction. Initial external runtime import support is PNG/JPEG sprites; Scene View drops intentionally create at world origin; dedicated Prefab Mode and broader media import remain outside this increment.

## 2026-07-29 Project Window Functional Parity Correction

Use the official Unity 5.4 Project Window documentation as the functional and structural reference for Dragon Pixel's Project dock. Replace the familiar-only target with a one-to-one supported local-project representation: Favorites/folder navigation, immediate content icons, toolbar and clickable breadcrumbs, icon-size slider with list mode, one/two-column layout, lock, saved searches, and documented keyboard behavior. Keep Dragon Pixel branding, theme, Qt/generated icons, service-owned mutation, formats, recovery, and evidence gates; do not copy Unity artwork, source code, Asset Store behavior, or serialized formats. Perform rendered Windows QA and update draft PR #7 without merging.
