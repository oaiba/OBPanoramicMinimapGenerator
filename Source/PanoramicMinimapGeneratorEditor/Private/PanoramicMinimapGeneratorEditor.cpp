#include "PanoramicMinimapGeneratorEditor.h"
#include "PanoramicMinimapGeneratorCommands.h"
#include "MinimapGeneratorWindow.h"
#include "LevelEditor.h" // BẮT BUỘC PHẢI INCLUDE FILE NÀY
#include "Widgets/Docking/SDockTab.h"
#include "Logging/LogMacros.h"

static const FName PanoramicMinimapGeneratorTabName("PanoramicMinimapGenerator");

#define LOCTEXT_NAMESPACE "FPanoramicMinimapGeneratorEditorModule"

void FPanoramicMinimapGeneratorEditorModule::StartupModule()
{
    // Đăng ký Tab Spawner trước để nó sẵn sàng khi được gọi
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(PanoramicMinimapGeneratorTabName, FOnSpawnTab::CreateRaw(this, &FPanoramicMinimapGeneratorEditorModule::OnSpawnPluginTab))
		.SetDisplayName(LOCTEXT("FPanoramicMinimapGeneratorTabTitle", "Panoramic Minimap"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

    // Đăng ký một callback để chạy hàm RegisterMenus() khi hệ thống ToolMenus sẵn sàng
    UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FPanoramicMinimapGeneratorEditorModule::RegisterMenus));
}

void FPanoramicMinimapGeneratorEditorModule::ShutdownModule()
{
    // Hủy đăng ký callback để tránh lỗi khi tắt Editor
    UToolMenus::UnRegisterStartupCallback(this);

    // Hủy đăng ký Tab Spawner
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(PanoramicMinimapGeneratorTabName);
}

void FPanoramicMinimapGeneratorEditorModule::RegisterMenus()
{
    // Tìm đến menu "Tools" trong Level Editor
    UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
    if (!Menu)
    {
        UE_LOG(LogTemp, Error, TEXT("[PanoramicMinimapGenerator] Failed to find 'LevelEditor.MainMenu.Tools' menu."));
        return;
    }

    // Thêm một section mới vào menu "Tools"
    FToolMenuSection& Section = Menu->AddSection(
        "PanoramicTools", // Tên định danh cho section
        LOCTEXT("PanoramicToolsSection", "Panoramic Tools") // Tên hiển thị của section
    );

    // Thêm mục menu của chúng ta vào section vừa tạo
    Section.AddMenuEntry(
        FName("OpenPanoramicMinimapGenerator"), // Tên định danh cho menu entry
        LOCTEXT("PanoramicMinimapGeneratorLabel", "Panoramic Minimap Generator"),
        LOCTEXT("PanoramicMinimapGeneratorTooltip", "Opens the Panoramic Minimap Generator window."),
        FSlateIcon(), // Bạn có thể thêm icon ở đây sau
        FUIAction(FExecuteAction::CreateRaw(this, &FPanoramicMinimapGeneratorEditorModule::PluginButtonClicked))
    );

    UE_LOG(LogTemp, Log, TEXT("[PanoramicMinimapGenerator] Menu registered successfully using UToolMenus."));
}

void FPanoramicMinimapGeneratorEditorModule::PluginButtonClicked()
{
	FGlobalTabmanager::Get()->TryInvokeTab(PanoramicMinimapGeneratorTabName);
}

TSharedRef<SDockTab> FPanoramicMinimapGeneratorEditorModule::OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SMinimapGeneratorWindow)
		];
}

IMPLEMENT_MODULE(FPanoramicMinimapGeneratorEditorModule, PanoramicMinimapGeneratorEditor)

#undef LOCTEXT_NAMESPACE
