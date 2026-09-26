' sample.vba — VBA syntax-highlighting sample for the UltraCanvas demo.
Option Explicit

Private Const TAX_RATE As Double = 0.19

Public Type Invoice
    Customer As String
    Net As Currency
    Paid As Boolean
End Type

Public Function Gross(ByVal net As Currency) As Currency
    Gross = Round(net * (1 + TAX_RATE), 2)
End Function

Public Sub ReportOpenInvoices(invoices() As Invoice)
    Dim i As Long, total As Currency
    Dim line As String

    For i = LBound(invoices) To UBound(invoices)
        If Not invoices(i).Paid Then
            total = total + Gross(invoices(i).Net)
            line = invoices(i).Customer & ": " & Format(Gross(invoices(i).Net), "0.00")
            Debug.Print line
        End If
    Next i

    Select Case total
        Case Is > 1000: MsgBox "Open total is high: " & total, vbExclamation
        Case 0: Debug.Print "Nothing open."
        Case Else: Debug.Print "Open total: " & total
    End Select
End Sub
