using UnrealBuildTool;

public class IrritatorCore : ModuleRules
{
	public IrritatorCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;

		// Noyau Irritator : aucune dependance gameplay (pas de CoreUObject/Engine).
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core"
		});
	}
}
