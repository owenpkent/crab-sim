// SPDX-License-Identifier: Apache-2.0
using UnrealBuildTool;
using System.Collections.Generic;

public class CrabSimEditorTarget : TargetRules
{
	public CrabSimEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("CrabSim");
	}
}
