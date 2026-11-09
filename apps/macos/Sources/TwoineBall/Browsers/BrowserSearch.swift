import AppKit
import SwiftUI

#if canImport(BallCore)
import BallCore
#endif

struct BrowserSearchField: NSViewRepresentable {
    @Binding var text: String
    @Binding var focused: Bool
    var placeholder = "Search Files"
    var identifier = "browserSearch"

    func makeCoordinator() -> Coordinator { Coordinator(self) }
    func makeNSView(context: Context) -> GlassSearchView {
        let view = GlassSearchView()
        view.field.delegate = context.coordinator
        view.field.setAccessibilityLabel(placeholder)
        view.field.setAccessibilityIdentifier(identifier)
        return view
    }
    func updateNSView(_ view: GlassSearchView, context: Context) {
        context.coordinator.parent = self
        let field = view.field
        field.placeholderString = placeholder
        if field.stringValue != text { field.stringValue = text }
        if focused, field.currentEditor() == nil, field.window?.makeFirstResponder(field) == true {
            field.currentEditor()?.selectedRange = NSRange(
                location: field.stringValue.utf16.count, length: 0)
        }
    }
    final class GlassSearchView: NSGlassEffectView {
        let field = NSSearchField()
        init() {
            super.init(frame: .zero)
            style = .regular
            effectIsInteractive = true
            cornerRadius = 16
            field.cell = GlassSearchCell(textCell: "")
            field.isBezeled = true
            field.isEditable = true
            field.isSelectable = true
            field.bezelStyle = .roundedBezel
            field.font = .systemFont(ofSize: NSFont.systemFontSize)
            field.drawsBackground = false
            field.sendsSearchStringImmediately = true
            field.translatesAutoresizingMaskIntoConstraints = false
            let content = NSView()
            content.addSubview(field)
            contentView = content
            NSLayoutConstraint.activate([
                field.leadingAnchor.constraint(equalTo: content.leadingAnchor, constant: 10),
                field.trailingAnchor.constraint(equalTo: content.trailingAnchor, constant: -10),
                field.centerYAnchor.constraint(equalTo: content.centerYAnchor),
                field.heightAnchor.constraint(equalToConstant: 22),
            ])
        }
        required init?(coder: NSCoder) { fatalError("Use init()") }
    }
    final class GlassSearchCell: NSSearchFieldCell {
        override func draw(withFrame frame: NSRect, in controlView: NSView) {
            drawInterior(withFrame: frame, in: controlView)
        }
    }
    final class Coordinator: NSObject, NSSearchFieldDelegate {
        var parent: BrowserSearchField
        init(_ parent: BrowserSearchField) { self.parent = parent }
        func controlTextDidChange(_ notification: Notification) {
            if let field = notification.object as? NSSearchField { parent.text = field.stringValue }
        }
        func controlTextDidBeginEditing(_ notification: Notification) { parent.focused = true }
        func controlTextDidEndEditing(_ notification: Notification) { parent.focused = false }
    }
}

struct BrowserTypeToSearch: ViewModifier {
    @Binding var query: String
    @Binding var focused: Bool
    func body(content: Content) -> some View {
        content.onKeyPress(phases: .down) { press in
            if press.modifiers == .command, press.characters.lowercased() == "f" {
                focused = true
                return .handled
            }
            guard press.modifiers.intersection([.command, .control, .option]).isEmpty,
                !press.characters.isEmpty,
                press.characters.unicodeScalars.allSatisfy({
                    !CharacterSet.controlCharacters.contains($0)
                }),
                !press.characters.trimmingCharacters(in: .whitespaces).isEmpty
            else { return .ignored }
            query += press.characters
            focused = true
            return .handled
        }
    }
}

extension GuestEntry {
    var sortSize: UInt64 { size ?? 0 }
    var sortModified: Double { modified ?? -.greatestFiniteMagnitude }
}

extension MediaDirectory.Entry {
    var sortModified: Double { modified ?? -.greatestFiniteMagnitude }
}

func browserSize(_ size: UInt64?, directory: Bool) -> String {
    guard !directory, let size else { return "—" }
    return ByteCountFormatter.string(fromByteCount: Int64(clamping: size), countStyle: .file)
}

/// Seconds since 1970 from 0001-01-01 to 9999-12-31 23:59:59 UTC; the formatter's range.
private let formattableDates: ClosedRange<Double> = -62_135_596_800...253_402_300_799

func browserDate(_ modified: Double?) -> String {
    guard let modified, modified.isFinite, formattableDates.contains(modified)
    else { return "—" }
    return Date(timeIntervalSince1970: modified).formatted(date: .abbreviated, time: .shortened)
}
