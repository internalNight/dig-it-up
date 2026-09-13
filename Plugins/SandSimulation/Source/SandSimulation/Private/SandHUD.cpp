#include "SandHUD.h"

#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "CanvasItem.h"
#include "Styling/CoreStyle.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "SandExcavatorPawn.h"
#include "SandRoadheaderPawn.h"
#include "SandPreviewGameMode.h"
#include "SandLevelSettings.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

void ASandHUD::DrawHUD()
{
    Super::DrawHUD();
    if (Canvas == nullptr || PlayerOwner == nullptr || GEngine == nullptr)
    {
        return;
    }

    if (PlayerOwner->WasInputKeyJustPressed(EKeys::H))
    {
        bShowControls = !bShowControls;
    }

    const float UiScale = FMath::Clamp(Canvas->SizeY / 720.0f, 0.75f, 1.35f);
    const float Margin = 18.0f * UiScale;
    const float LineHeight = 22.0f * UiScale;
    UFont* Font = GEngine->GetSmallFont();
    auto* Mode=Cast<ASandPreviewGameMode>(GetWorld()->GetAuthGameMode());
    if(Mode && Mode->IsSelectingVehicle())
    {
        DrawRect(FLinearColor(.018f,.025f,.045f,1),0,0,Canvas->SizeX,Canvas->SizeY);
        const float X=Canvas->SizeX*.5f-320*UiScale, Y=Canvas->SizeY*.35f;
        DrawText(TEXT("DIG IT UP  /  CHOOSE YOUR MACHINE"),FLinearColor(1,.7f,.2f),X,Y-70*UiScale,Font,1.6f*UiScale);
        DrawRect(FLinearColor(.14f,.19f,.24f,1),X,Y,310*UiScale,160*UiScale);
        DrawRect(FLinearColor(.22f,.16f,.08f,1),X+330*UiScale,Y,310*UiScale,160*UiScale);
        DrawText(TEXT("1   EXCAVATOR"),FLinearColor::White,X+15*UiScale,Y+20*UiScale,Font,1.3f*UiScale);
        DrawText(TEXT("Dig, lift and place with a bucket"),FLinearColor::White,X+15*UiScale,Y+65*UiScale,Font,UiScale);
        DrawText(TEXT("2   ROADHEADER"),FLinearColor(1,.75f,.3f),X+345*UiScale,Y+20*UiScale,Font,1.3f*UiScale);
        DrawText(TEXT("Rotating drum + scraper conveyor"),FLinearColor::White,X+345*UiScale,Y+65*UiScale,Font,UiScale);
        DrawText(TEXT("Click a machine or press 1 / 2"),FLinearColor(.7f,.8f,.9f),X,Y+190*UiScale,Font,UiScale);
        AddHitBox(FVector2D(X,Y),FVector2D(310,160)*UiScale,TEXT("Excavator"),true);
        AddHitBox(FVector2D(X+330*UiScale,Y),FVector2D(310,160)*UiScale,TEXT("Roadheader"),true);
        if(PlayerOwner->WasInputKeyJustPressed(EKeys::One)) Mode->SelectVehicle(false);
        if(PlayerOwner->WasInputKeyJustPressed(EKeys::Two)) Mode->SelectVehicle(true);
        if(PlayerOwner->WasInputKeyJustPressed(EKeys::Escape)) PlayerOwner->ConsoleCommand(TEXT("quit"));
        return;
    }
    if (Mode && Mode->IsVictoryVisible())
    {
        const float CentreX = Canvas->SizeX * 0.5f;
        DrawRect(FLinearColor(0.005f,0.01f,0.02f,0.76f),0,0,Canvas->SizeX,Canvas->SizeY);
        // Rasterize a runtime font at the final size: magnifying the legacy
        // small bitmap font makes a large celebration visibly blurred.
        const float TitlePoints = FMath::Min(Canvas->SizeX * 0.105f, Canvas->SizeY * 0.14f);
        if (!VictoryFont)
        {
            VictoryFont = NewObject<UFont>(this);
            VictoryFont->FontCacheType = EFontCacheType::Runtime;
        }
        FCanvasTextItem Title(FVector2D(CentreX,Canvas->SizeY*0.32f),FText::FromString(TEXT("VICTORY")),
            FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),TitlePoints),FLinearColor(1.0f,0.72f,0.12f));
        Title.Font = VictoryFont;
        Title.bCentreX = Title.bCentreY = true;
        Title.EnableShadow(FLinearColor(0.22f,0.10f,0.015f),FVector2D(0,5*UiScale));
        Canvas->DrawItem(Title);
        auto DrawCentred = [&](const TCHAR* Text, float Y, float Scale, FLinearColor Color)
        {
            float W,H;
            GetTextSize(Text,W,H,Font,Scale);
            DrawText(Text,Color,CentreX-W*0.5f,Y,Font,Scale,false);
        };
        DrawRect(FLinearColor(1,0.72f,0.12f),CentreX-100*UiScale,Canvas->SizeY*0.49f,200*UiScale,2*UiScale);
        DrawCentred(TEXT("You dug it up!"),Canvas->SizeY*0.52f,1.8f*UiScale,FLinearColor(1,.94f,.78f));
        const float X = CentreX-260*UiScale, Y = Canvas->SizeY*0.64f;
        DrawRect(FLinearColor(0.12f,0.28f,0.23f),X,Y,310*UiScale,60*UiScale);
        DrawRect(FLinearColor(0.30f,0.12f,0.10f),X+330*UiScale,Y,190*UiScale,60*UiScale);
        DrawText(TEXT("Continue Digging"),FLinearColor::White,X+47*UiScale,Y+20*UiScale,Font,1.4f*UiScale,false);
        DrawText(TEXT("Exit Game"),FLinearColor::White,X+365*UiScale,Y+20*UiScale,Font,1.4f*UiScale,false);
        DrawCentred(TEXT("ENTER  Continue     ESC  Exit"),Y+83*UiScale,UiScale,FLinearColor(.75f,.78f,.82f));
        AddHitBox(FVector2D(X,Y),FVector2D(310,60)*UiScale,TEXT("Continue"),true);
        AddHitBox(FVector2D(X+330*UiScale,Y),FVector2D(190,60)*UiScale,TEXT("Exit"),true);
        if (PlayerOwner->WasInputKeyJustPressed(EKeys::Enter)) { NotifyHitBoxClick(TEXT("Continue")); }
        if (PlayerOwner->WasInputKeyJustPressed(EKeys::Escape)) { NotifyHitBoxClick(TEXT("Exit")); }
        return;
    }

    const ASandExcavatorPawn* Excavator = Cast<ASandExcavatorPawn>(PlayerOwner->GetPawn());
    const auto* Machine=Cast<ASandRoadheaderPawn>(Excavator);
    const float SpeedKmh = Excavator != nullptr
        ? Excavator->GetVelocity().Size2D() * 0.036f
        : 0.0f;
    const int32 Supports = Excavator != nullptr ? Excavator->GetGroundedSupportCount() : 0;

    DrawRect(FLinearColor(0.015f, 0.02f, 0.025f, 0.76f), Margin, Margin, 370.0f * UiScale, 80.0f * UiScale);
    DrawText(Machine ? TEXT("DIG IT UP  /  ROADHEADER") : TEXT("DIG IT UP  /  EASY"), FLinearColor(1.0f, 0.70f, 0.12f),
        Margin + 12.0f * UiScale, Margin + 8.0f * UiScale, Font, 1.12f * UiScale, false);
    DrawText(FString::Printf(TEXT("Speed  %4.1f km/h    Track support  %d / 4"), SpeedKmh, Supports),
        FLinearColor::White, Margin + 12.0f * UiScale, Margin + 31.0f * UiScale,
        Font, 0.92f * UiScale, false);
    DrawText(FParse::Param(FCommandLine::Get(),TEXT("SandRoadheaderBench")) ? TEXT("Transport bench  |  Press T to run motors") : Mode && Mode->HasWon() ? TEXT("VICTORY COMPLETE  -  Free exploration") :
        *FString::Printf(TEXT("Dig %.1f m down. Uncover the RED floor."),GetDefault<USandLevelSettings>()->SandDepthMeters),
        FLinearColor(1.0f,0.65f,0.65f),Margin+12*UiScale,Margin+54*UiScale,Font,0.95f*UiScale,false);
    if (Mode && !Mode->HasWon() && Mode->GetVictoryCountdown()>=0.0f)
    {
        DrawRect(FLinearColor(0.1f,0.02f,0.02f,0.88f),Canvas->SizeX*0.5f-185*UiScale,Canvas->SizeY-75*UiScale,370*UiScale,48*UiScale);
        DrawText(FString::Printf(TEXT("RED FLOOR FOUND!   %.1f s"),Mode->GetVictoryCountdown()),
            FLinearColor(1,0.85f,0.55f),Canvas->SizeX*0.5f-145*UiScale,Canvas->SizeY-60*UiScale,Font,1.15f*UiScale,false);
    }

    if (bShowControls)
    {
        const float PanelY = Margin + 90.0f * UiScale;
        DrawRect(FLinearColor(0.015f, 0.02f, 0.025f, 0.72f),
            Margin, PanelY, 270.0f * UiScale, 212.0f * UiScale);
        if(Machine)
        {
            DrawText(FString::Printf(TEXT("%s head  |  %.1f rpm  |  Chain %.2f m/s  |  Load %.1f / 240 Nm%s"),*Machine->GetHeadType(),Machine->GetDrumRPM(),Machine->GetConveyorSpeed(),Machine->GetLoadTorque(),FParse::Param(FCommandLine::Get(),TEXT("SandHeadInspect"))?TEXT("  |  CUTAWAY: casing contacts active"):TEXT("")),
                FLinearColor(1,.8f,.3f),Margin,Canvas->SizeY-40*UiScale,Font,UiScale);
        }
        const TCHAR* Lines[] =
        {
            TEXT("W / S       Drive forward / reverse"),
            TEXT("A / D       Steer left / right"),
            TEXT("SPACE       Brake"),
            Machine ? TEXT("Q / E       Cutter raise / lower") : TEXT("Q / E       Boom up / down"),
            Machine ? TEXT("T / G       Motor on-off / reverse") : TEXT("R / F       Stick in / out"),
            Machine ? TEXT("C  View     P  Debug material points") : TEXT("T / G       Bucket curl / dump"),
            TEXT("H           Hide / show this help"),
            TEXT("ESC         Quit game")
        };
        for (int32 LineIndex = 0; LineIndex < UE_ARRAY_COUNT(Lines); ++LineIndex)
        {
            const FLinearColor Color = LineIndex < 3
                ? FLinearColor(0.86f, 0.91f, 1.0f)
                : FLinearColor(1.0f, 0.82f, 0.42f);
            DrawText(Lines[LineIndex], Color,
                Margin + 12.0f * UiScale,
                PanelY + 12.0f * UiScale + LineIndex * LineHeight,
                Font, 0.96f * UiScale, false);
        }
    }
    else
    {
        DrawText(TEXT("H  Show controls"), FLinearColor::White,
            Margin, Margin + 96.0f * UiScale, Font, 0.9f * UiScale, false);
    }

    // Small unobtrusive aim reference helps judge bucket position and depth.
    const float CentreX = Canvas->SizeX * 0.5f;
    const float CentreY = Canvas->SizeY * 0.5f;
    const FLinearColor ReticleColor(1.0f, 1.0f, 1.0f, 0.35f);
    DrawLine(CentreX - 5.0f, CentreY, CentreX + 5.0f, CentreY, ReticleColor, 1.0f);
    DrawLine(CentreX, CentreY - 5.0f, CentreX, CentreY + 5.0f, ReticleColor, 1.0f);
}

void ASandHUD::NotifyHitBoxClick(FName BoxName)
{
    auto* Mode=Cast<ASandPreviewGameMode>(GetWorld()->GetAuthGameMode());
    if(Mode && Mode->IsSelectingVehicle())
    {
        if(BoxName==TEXT("Excavator")) Mode->SelectVehicle(false);
        if(BoxName==TEXT("Roadheader")) Mode->SelectVehicle(true);
        return;
    }
    if (!Mode || !Mode->IsVictoryVisible()) { return; }
    if (BoxName==TEXT("Continue")) { Mode->ContinueExploring(); }
    else if (BoxName==TEXT("Exit") && PlayerOwner) { PlayerOwner->ConsoleCommand(TEXT("quit")); }
}
