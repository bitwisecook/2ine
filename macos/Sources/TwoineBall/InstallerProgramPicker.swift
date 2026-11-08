import AppKit
import SwiftUI

struct InstallerProgramPicker: NSViewRepresentable {
    @Binding var program: String
    let suggestions: [String]
    func makeCoordinator() -> Coordinator { Coordinator(self) }
    func makeNSView(context: Context) -> NSComboBox {
        let field = NSComboBox()
        field.isEditable = true
        field.completes = true
        field.hasVerticalScroller = true
        field.numberOfVisibleItems = 12
        field.placeholderString = "Choose or enter an installer"
        field.font = .systemFont(ofSize: NSFont.systemFontSize)
        field.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        field.setAccessibilityLabel("Installer program")
        field.setAccessibilityIdentifier("programPath")
        field.delegate = context.coordinator
        update(field, coordinator: context.coordinator)
        return field
    }
    func updateNSView(_ field: NSComboBox, context: Context) {
        context.coordinator.parent = self
        update(field, coordinator: context.coordinator)
    }
    private func update(_ field: NSComboBox, coordinator: Coordinator) {
        if coordinator.options != suggestions {
            coordinator.options = suggestions
            field.removeAllItems()
            field.addItems(withObjectValues: suggestions)
        }
        if field.stringValue != program { field.stringValue = program }
    }
    @MainActor final class Coordinator: NSObject, NSComboBoxDelegate {
        var parent: InstallerProgramPicker
        var options: [String] = []
        init(_ parent: InstallerProgramPicker) { self.parent = parent }
        func controlTextDidChange(_ notification: Notification) {
            guard let field = notification.object as? NSComboBox else { return }
            parent.program = field.stringValue
        }
        func comboBoxSelectionDidChange(_ notification: Notification) {
            guard let field = notification.object as? NSComboBox,
                options.indices.contains(field.indexOfSelectedItem)
            else { return }
            parent.program = options[field.indexOfSelectedItem]
        }
    }
}
