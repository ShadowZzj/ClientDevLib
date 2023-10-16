object Form1: TForm1
  Left = 501
  Top = 128
  Caption = 'Macros Example'
  ClientHeight = 189
  ClientWidth = 318
  Color = clBtnFace
  Font.Charset = DEFAULT_CHARSET
  Font.Color = clWindowText
  Font.Height = -11
  Font.Name = 'MS Sans Serif'
  Font.Style = []
  OldCreateOrder = False
  PixelsPerInch = 96
  TextHeight = 13
  object GroupBox3: TGroupBox
    Left = 32
    Top = 24
    Width = 265
    Height = 137
    Caption = 'Virtualization Macros'
    TabOrder = 0
    object Button5: TButton
      Left = 24
      Top = 24
      Width = 193
      Height = 25
      Caption = 'Execute TIGER WHITE Macro'
      TabOrder = 0
      OnClick = Button5Click
    end
    object Button6: TButton
      Left = 24
      Top = 56
      Width = 193
      Height = 25
      Caption = 'Execute FISH WHITE Macro'
      TabOrder = 1
      OnClick = Button6Click
    end
    object Button7: TButton
      Left = 24
      Top = 88
      Width = 193
      Height = 25
      Caption = 'Execute FISH RED Macro'
      TabOrder = 2
      OnClick = Button7Click
    end
  end
end
