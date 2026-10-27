using System.IO;
using UnrealBuildTool;

public class Earth315k : ModuleRules
{
	public Earth315k(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"ProceduralMeshComponent",
			"Json"
		});

		// The simulation core lives outside Unreal so it can also run headless and be tested on its own.
		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "..", "..", "Simulation", "include"));
	}
}
