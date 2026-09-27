// SPDX-License-Identifier: Apache-2.0
using UnrealBuildTool;

public class CrabSim : ModuleRules
{
	public CrabSim(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// The test files each bring their own TestFlags into scope with a using-directive, so they only compile apart.
		// Adaptive unity regroups files as the git working set changes, and merged them into one ambiguous blob.
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"Slate",
			"SlateCore",
			"ProceduralMeshComponent",
			"AssetRegistry"
		});

		// Headers live flat in Source/CrabSim/; keep subdirectories (Tests/) able to include them.
		PrivateIncludePaths.Add(ModuleDirectory);
	}
}
