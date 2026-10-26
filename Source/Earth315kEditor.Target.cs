using UnrealBuildTool;

public class Earth315kEditorTarget : TargetRules
{
	public Earth315kEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("Earth315k");
	}
}
