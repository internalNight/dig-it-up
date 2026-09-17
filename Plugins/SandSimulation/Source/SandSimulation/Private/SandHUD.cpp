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

namespace
{
FVector2D FixedDriveBase(const float Width, const float Height)
{
    return FVector2D(0.16f * Width, 0.75f * Height);
}
}

bool ASandHUD::UsesMobileControls() const
{
#if PLATFORM_ANDROID
    return true;
#else
    return FParse::Param(FCommandLine::Get(), TEXT("SandTouchPreview"));
#endif
}

void ASandHUD::PollMobileControls()
{
    DriveInput = FVector2D::ZeroVector;
    LeverInput[0] = LeverInput[1] = LeverInput[2] = 0.0f;
    if (!UsesMobileControls() || !PlayerOwner)
    {
        return;
    }

    int32 Width = 0, Height = 0;
    PlayerOwner->GetViewportSize(Width, Height);
    if (Width <= 0 || Height <= 0)
    {
        return;
    }
    const float ScreenW = static_cast<float>(Width);
    const float ScreenH = static_cast<float>(Height);
    const float Radius = 0.15f * ScreenH;
    const FVector2D DriveBase = FixedDriveBase(ScreenW, ScreenH);
    const float LeverTravel = 0.17f * ScreenH;
    const float LeverX[3] = {0.67f * ScreenW, 0.79f * ScreenW, 0.91f * ScreenW};

    // Keep each finger on the control where it first touched. A finger sliding
    // across controls cannot accidentally move another joint.
    for (int32 Finger = 0; Finger < UE_ARRAY_COUNT(Touches); ++Finger)
    {
        float X = 0.0f, Y = 0.0f;
        bool bPressed = false;
        PlayerOwner->GetInputTouchState(static_cast<ETouchIndex::Type>(Finger), X, Y, bPressed);
        FTouchCapture& Capture = Touches[Finger];
        if (!bPressed)
        {
            Capture = FTouchCapture();
            continue;
        }
        const FVector2D Point(X, Y);
        if (!Capture.bDown)
        {
            Capture.bDown = true;
            Capture.Start = Point;
            Capture.Control = -1;
            if (FVector2D::Distance(Point, DriveBase) <= 1.35f * Radius)
            {
                Capture.Control = 0;
            }
            else if (X > 0.60f * ScreenW && Y > 0.44f * ScreenH && Y < 0.96f * ScreenH)
            {
                for (int32 Lever = 0; Lever < 3; ++Lever)
                {
                    if (FMath::Abs(X - LeverX[Lever]) < 0.052f * ScreenW)
                    {
                        Capture.Control = static_cast<int8>(Lever + 1);
                        break;
                    }
                }
            }
            // One finger per control; the first finger keeps ownership.
            for (int32 Other = 0; Other < UE_ARRAY_COUNT(Touches); ++Other)
            {
                if (Other != Finger && Capture.Control >= 0 && Touches[Other].bDown &&
                    Touches[Other].Control == Capture.Control)
                {
                    Capture.Control = -1;
                    break;
                }
            }
        }
        Capture.Position = Point;
    }

    for (const FTouchCapture& Capture : Touches)
    {
        if (!Capture.bDown || Capture.Control < 0)
        {
            continue;
        }
        if (Capture.Control == 0)
        {
            const FVector2D Drag = (Capture.Position - DriveBase) / Radius;
            DriveInput = Drag.GetClampedToMaxSize(1.0);
            DriveInput.Y = -DriveInput.Y;
            if (DriveInput.Size() < 0.07)
            {
                DriveInput = FVector2D::ZeroVector;
            }
        }
        else
        {
            float Value = FMath::Clamp((Capture.Start.Y - Capture.Position.Y) / LeverTravel, -1.0f, 1.0f);
            LeverInput[Capture.Control - 1] = FMath::Abs(Value) < 0.06f ? 0.0f : Value;
        }
    }
}

void ASandHUD::GetMobileControls(float& Throttle, float& Steering, float& Boom,
    float& Stick, float& Bucket) const
{
    Throttle = static_cast<float>(DriveInput.Y);
    Steering = static_cast<float>(DriveInput.X);
    Boom = LeverInput[0];
    Stick = LeverInput[1];
    Bucket = LeverInput[2];
}

void ASandHUD::DrawMobileControls()
{
    const float W = Canvas->SizeX, H = Canvas->SizeY;
    const float S = FMath::Clamp(H / 720.0f, 0.8f, 1.5f);
    const float Radius = 0.15f * H;
    const FVector2D Base = FixedDriveBase(W, H);
    const FVector2D Knob = Base + FVector2D(DriveInput.X, -DriveInput.Y) * Radius;
    const FLinearColor Amber(1.0f, 0.72f, 0.16f, 0.9f);
    const FLinearColor White(0.88f, 0.93f, 1.0f, 0.9f);
    constexpr int32 Segments = 32;
    for (int32 Part = 0; Part < Segments; ++Part)
    {
        const float A = 2.0f * PI * Part / Segments;
        const float B = 2.0f * PI * (Part + 1) / Segments;
        DrawLine(Base.X + FMath::Cos(A) * Radius, Base.Y + FMath::Sin(A) * Radius,
            Base.X + FMath::Cos(B) * Radius, Base.Y + FMath::Sin(B) * Radius, White, 2.5f * S);
    }
    DrawRect(Amber, Knob.X - 20.0f * S, Knob.Y - 20.0f * S, 40.0f * S, 40.0f * S);
    DrawText(TEXT("DRIVE / STEER"), White, Base.X - 48.0f * S,
        FMath::Min(H - 29.0f * S, Base.Y + Radius + 13.0f * S), GEngine->GetSmallFont(), S);

    const float Xs[3] = {0.67f * W, 0.79f * W, 0.91f * W};
    const TCHAR* Names[3] = {TEXT("BOOM"), TEXT("STICK"), TEXT("BUCKET")};
    const float CentreY = 0.74f * H, Travel = 0.17f * H;
    for (int32 Lever = 0; Lever < 3; ++Lever)
    {
        const float X = Xs[Lever];
        DrawRect(FLinearColor(0.02f, 0.04f, 0.06f, 0.5f), X - 43.0f * S,
            CentreY - Travel - 45.0f * S, 86.0f * S, 2.0f * Travel + 93.0f * S);
        DrawText(Names[Lever], White, X - 27.0f * S, CentreY - Travel - 35.0f * S,
            GEngine->GetSmallFont(), S);
        DrawRect(FLinearColor(0.55f, 0.65f, 0.75f, 0.8f), X - 3.0f * S,
            CentreY - Travel, 6.0f * S, 2.0f * Travel);
        DrawRect(White, X - 27.0f * S, CentreY - 1.0f * S, 54.0f * S, 2.0f * S);
        DrawRect(Amber, X - 26.0f * S, CentreY - LeverInput[Lever] * Travel - 15.0f * S,
            52.0f * S, 30.0f * S);
        DrawText(TEXT("UP  /  DOWN"), White, X - 38.0f * S,
            CentreY + Travel + 14.0f * S, GEngine->GetSmallFont(), 0.85f * S);
    }
}

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
    FString SoilCase;
    if(FParse::Value(FCommandLine::Get(),TEXT("SandSoilBench="),SoilCase)) {
        float Angle=60,Depth=.1f;
        FParse::Value(FCommandLine::Get(),TEXT("SandBladeAngle="),Angle);
        FParse::Value(FCommandLine::Get(),TEXT("SandBladeDepth="),Depth);
        DrawRect(FLinearColor(.015f,.02f,.025f,.8f),Margin,Margin,450*UiScale,80*UiScale);
        DrawText(TEXT("EXCAVATION LAB  /  SOIL BENCH"),FLinearColor(1,.7f,.12f),Margin+12*UiScale,Margin+8*UiScale,Font,UiScale);
        DrawText(FString::Printf(TEXT("%s | Blade %.0f deg to horizontal | Depth %.2f m"),*SoilCase,Angle,Depth),FLinearColor::White,Margin+12*UiScale,Margin+31*UiScale,Font,.9f*UiScale);
        DrawText(TEXT("Numerical experiment - material not calibrated"),FLinearColor(1,.65f,.65f),Margin+12*UiScale,Margin+54*UiScale,Font,.9f*UiScale);
        return;
    }
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

    if (bShowControls && !UsesMobileControls())
    {
        const float PanelY = Margin + 90.0f * UiScale;
        DrawRect(FLinearColor(0.015f, 0.02f, 0.025f, 0.72f),
            Margin, PanelY, 270.0f * UiScale, 212.0f * UiScale);
        if(Machine)
        {
            DrawText(FString::Printf(TEXT("In conveyor %.2f kg  |  Tail gate %.2f kg  |  Rear settled %.2f kg"),Machine->GetTroughMass(),Machine->GetTailCrossMass(),Machine->GetRearSettledMass()),
                FLinearColor(.7f,1,.9f),Margin,Canvas->SizeY-90*UiScale,Font,UiScale);
            if(FParse::Param(FCommandLine::Get(),TEXT("SandFeedControl")) || FParse::Param(FCommandLine::Get(),TEXT("SandAdaptiveFeed")))
                DrawText(FString::Printf(TEXT("Depth %s | Feed %.0f%% | Traction %.0f N | Target %.3f m/s"),Machine->GetDepthStatus(),100*Machine->GetFeedFraction(),Machine->GetTractionBudgetN(),Machine->GetDriveTargetMps()),
                    FLinearColor(.6f,1,.8f),Margin,Canvas->SizeY-65*UiScale,Font,.9f*UiScale);
            DrawText(FString::Printf(TEXT("%s head  |  %.1f rpm  |  Chain %.2f m/s  |  Load %.1f / 240 Nm%s"),*Machine->GetHeadType(),Machine->GetDrumRPM(),Machine->GetConveyorSpeed(),Machine->GetLoadTorque(),FParse::Param(FCommandLine::Get(),TEXT("SandHeadInspect"))?TEXT("  |  CUTAWAY: casing contacts active"):TEXT("")),
                FLinearColor(1,.8f,.3f),Margin,Canvas->SizeY-40*UiScale,Font,UiScale);
        }
        const TCHAR* Lines[] =
        {
            TEXT("W / S       Drive forward / reverse"),
            TEXT("A / D       Steer left / right"),
            TEXT("SPACE       Brake"),
            Machine ? TEXT("Q/E  Raise/lower   R  Auto depth") : TEXT("Q / E       Boom up / down"),
            Machine ? TEXT("T / G       Motor on-off / reverse") : TEXT("R / F       Stick in / out"),
            Machine ? TEXT("C View  RMB drag/arrows Orbit  P Points") : TEXT("T / G       Bucket curl / dump"),
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
    else if (!UsesMobileControls())
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
    if (UsesMobileControls() && Excavator && !Machine)
    {
        DrawMobileControls();
    }
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
