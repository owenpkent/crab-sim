// SPDX-License-Identifier: Apache-2.0
using UnrealBuildTool;
using System.Collections.Generic;

public class CrabSimTarget : TargetRules
{
	public CrabSimTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("CrabSim");
	}
}
