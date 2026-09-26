// SPDX-License-Identifier: Apache-2.0
using UnrealBuildTool;

public class CrabSim : ModuleRules
{
	public CrabSim(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"Slate",
			"SlateCore",
			"ProceduralMeshComponent"
		});

		// Headers live flat in Source/CrabSim/; keep subdirectories (Tests/) able to include them.
		PrivateIncludePaths.Add(ModuleDirectory);
	}
}
