{ sample.pas — Pascal syntax-highlighting sample for the UltraCanvas demo. }
program ShapeDemo;

uses
  SysUtils;

type
  TPoint = record
    X, Y: Double;
  end;

  TShape = class
  private
    FName: string;
  public
    constructor Create(const AName: string);
    function Area: Double; virtual; abstract;
    property Name: string read FName;
  end;

  TCircle = class(TShape)
  private
    FRadius: Double;
  public
    constructor Create(ARadius: Double);
    function Area: Double; override;
  end;

constructor TShape.Create(const AName: string);
begin
  FName := AName;
end;

constructor TCircle.Create(ARadius: Double);
begin
  inherited Create('circle');
  FRadius := ARadius;
end;

function TCircle.Area: Double;
begin
  Result := Pi * FRadius * FRadius;
end;

var
  Circle: TCircle;
  I: Integer;
begin
  Circle := TCircle.Create(2.5);
  try
    WriteLn(Format('Shape "%s" has area %.4f', [Circle.Name, Circle.Area]));
    for I := 1 to 3 do
      WriteLn('Iteration ', I);
  finally
    Circle.Free;
  end;
end.
