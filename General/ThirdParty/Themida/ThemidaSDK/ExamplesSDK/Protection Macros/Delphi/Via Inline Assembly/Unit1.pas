unit Unit1;

interface

uses
  Windows, Messages, SysUtils, Variants, Classes, Graphics, Controls, Forms,
  Dialogs, StdCtrls, SecureEngineSDK;

type
  TForm1 = class(TForm)
    GroupBox3: TGroupBox;
    Button5: TButton;
    Button6: TButton;
    Button7: TButton;
    procedure Button5Click(Sender: TObject);
    procedure Button6Click(Sender: TObject);
    procedure Button7Click(Sender: TObject);

  private
    { Private declarations }
  public
    { Public declarations }
  end;

var
  Form1: TForm1;

implementation

{$R *.dfm}

procedure TForm1.Button5Click(Sender: TObject);

var
  Value, i: Integer;

begin

    Value := 0;

    // the following code, will be virtualized

    VM_TIGER_WHITE_START;

	  for i := 0 to  10 do
      Value := Value * 3;

     MessageBox(0, 'The TIGER WHITE Macro has successfully been executed', 'VM Macro', MB_OK + MB_ICONINFORMATION);

    VM_TIGER_WHITE_END;

end;

procedure TForm1.Button6Click(Sender: TObject);

var
  Value, i: Integer;

begin

    Value := 0;

    // the following code, will be virtualized

    VM_FISH_WHITE_START;

	  for i := 0 to  10 do
      Value := Value * 3 + i * 2;

     MessageBox(0, 'The FISH WHITE Macro has successfully been executed', 'VM Macro', MB_OK + MB_ICONINFORMATION);

    VM_FISH_WHITE_END;

end;

procedure TForm1.Button7Click(Sender: TObject);

var
  Value, i: Integer;

begin

    Value := 0;

    // the following code, will be virtualized

    VM_FISH_RED_START;

	  for i := 0 to  10 do
      Value := Value * 3 * i;

     MessageBox(0, 'The FISH RED Macro has successfully been executed', 'VM Macro', MB_OK + MB_ICONINFORMATION);

    VM_FISH_RED_END;

end;


end.
