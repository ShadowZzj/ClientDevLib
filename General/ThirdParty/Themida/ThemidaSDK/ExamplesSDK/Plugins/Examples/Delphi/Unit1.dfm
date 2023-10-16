object Form1: TForm1
  Left = 549
  Top = 201
  Width = 440
  Height = 252
  Caption = 'Form1'
  Color = clBtnFace
  Font.Charset = DEFAULT_CHARSET
  Font.Color = clWindowText
  Font.Height = -12
  Font.Name = 'MS Sans Serif'
  Font.Style = []
  OldCreateOrder = False
  Position = poScreenCenter
  PixelsPerInch = 107
  TextHeight = 13
  object labelStatus: TLabel
    Left = 8
    Top = 16
    Width = 102
    Height = 24
    Alignment = taCenter
    Caption = 'labelStatus'
    Font.Charset = DEFAULT_CHARSET
    Font.Color = clNavy
    Font.Height = -19
    Font.Name = 'MS Sans Serif'
    Font.Style = [fsBold]
    ParentFont = False
  end
  object lbMessage: TLabel
    Left = 8
    Top = 48
    Width = 3
    Height = 13
  end
  object Button1: TButton
    Left = 168
    Top = 128
    Width = 75
    Height = 25
    Caption = 'OK'
    TabOrder = 0
    OnClick = Button1Click
  end
end
