import BallCore
import SwiftUI

struct DependencyAdviceView: View {
    @Bindable var model: AppModel
    let audit: DependencyAudit?

    var body: some View {
        if let audit, audit.hasComponentAdvice {
            HStack(alignment: .top, spacing: 8) {
                Image(systemName: "info.circle").accessibilityHidden(true)
                VStack(alignment: .leading, spacing: 4) {
                    if audit.stale == true {
                        Text("The library check is out of date. Check again to see which OS/2 components are missing.")
                        Button("Check Libraries") { model.auditDependencies() }
                            .disabled(model.busy || model.mediaBusy || model.processRunning)
                    } else {
                        if !(audit.componentGroups ?? []).isEmpty {
                            Text("Missing OS/2 components — click a component to find it, then tick it to install:")
                            ForEach(audit.componentGroups ?? []) { group in
                                if let category = OS2ExtractionCategory(rawValue: group.id) {
                                    Button(group.title) { model.showOS2ComponentGroup(category, selecting: false) }
                                        .accessibilityIdentifier("missingComponent-" + group.id)
                                        .help("Show and highlight " + group.title + " in OS/2 Components")
                                } else {
                                    Text(group.title)
                                }
                            }
                        }
                        if !audit.ungroupedMissingLibraries.isEmpty {
                            Text("Other missing libraries: " + audit.ungroupedMissingLibraries.joined(separator: ", ") + ".")
                            Text("Some may belong to optional features or require separate software. See the audit for details.")
                        }
                        if (audit.componentGroups ?? []).isEmpty && audit.missing.isEmpty {
                            Text("OS/2 support files are missing. See the audit for details.")
                        }
                        if let log = model.dependencyAuditLog {
                            Button("Open Dependency Audit") { model.openFile(log.path) }
                        }
                    }
                }
                .buttonStyle(.link)
                .textSelection(.enabled)
                .fixedSize(horizontal: false, vertical: true)
            }
            .font(.caption).foregroundStyle(.secondary)
            .frame(maxWidth: .infinity, alignment: .leading)
            .accessibilityIdentifier("missingComponentsAdvice")
        }
    }
}
