import BallCore
import SwiftUI

struct RenameBallSheet: View {
    @Bindable var model: AppModel
    let request: BallRenameRequest
    @State private var name: String
    @State private var error: String?
    @FocusState private var nameFocused: Bool

    init(model: AppModel, request: BallRenameRequest) {
        self.model = model; self.request = request
        _name = State(initialValue: AppModel.displayName(request.path))
    }
    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("Rename Ball").font(.headline)
            Text(DisplayPath.homeAbbreviated(request.path)).font(.caption).foregroundStyle(.secondary)
                .textSelection(.enabled)
            TextField("Name:", text: $name).focused($nameFocused)
                .accessibilityIdentifier("renameBallName")
            if let error { Text(error).foregroundStyle(.red).fixedSize(horizontal: false, vertical: true) }
            HStack {
                Button("Cancel", role: .cancel) { model.ballRename = nil }.keyboardShortcut(.cancelAction)
                Spacer()
                Button("Rename") {
                    Task {
                        do { try await model.renameBall(request.path, to: name, expectedIdentity: request.identity); model.ballRename = nil }
                        catch { self.error = error.localizedDescription }
                    }
                }.keyboardShortcut(.defaultAction).accessibilityIdentifier("confirmRenameBall")
                    .disabled(name.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
            }.disabled(model.busy)
        }.padding(24).frame(width: 420).onAppear { nameFocused = true }
        .interactiveDismissDisabled(model.busy)
    }
}
