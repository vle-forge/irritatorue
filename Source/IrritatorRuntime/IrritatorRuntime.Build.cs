using UnrealBuildTool;

public class IrritatorRuntime : ModuleRules
{
	public IrritatorRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"IrritatorCore"
		});

		// Projects: the tests find their files in the directory of the plug-in.
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Projects"
		});

		// The session reads the projects and the components with the file
		// system, not from the pak files: Content/Irritator is staged as is.
		if (Target.ProjectFile != null)
		{
			RuntimeDependencies.Add("$(ProjectDir)/Content/Irritator/...", StagedFileType.NonUFS);
		}
	}
}
