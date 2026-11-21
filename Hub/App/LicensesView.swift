//
//  LicensesView.swift — Licences open source (obligation de notice GPL).
//
//  Les textes viennent du paquet : THIRD_PARTY_NOTICES.md et la GPL, copiés
//  tels quels depuis la racine du dépôt à la construction. L'adresse des
//  sources correspondantes vient de l'Info.plist (HubCorrespondingSourceURL).
//

import SwiftUI

struct LicensesView: View {
    static let sourceURL: URL? = {
        guard let s = Bundle.main.object(forInfoDictionaryKey: "HubCorrespondingSourceURL") as? String,
              !s.isEmpty else { return nil }
        return URL(string: s)
    }()

    var body: some View {
        List {
            Section {
                Text("SDR Driver Hub includes librtlsdr (osmocom rtl-sdr), free software licensed under the GNU General Public License version 2 or later. You may redistribute and modify it under the terms of that licence.")
                    .font(.footnote)
            }
            Section("Components") {
                NavigationLink {
                    LicenseText(title: "Third-party notices", resource: "THIRD_PARTY_NOTICES", ext: "md")
                } label: {
                    LabeledContent("librtlsdr", value: "GPL-2.0-or-later")
                }
                NavigationLink("GNU General Public License v2") {
                    LicenseText(title: "GPL-2.0", resource: "GPL-2.0-or-later", ext: "txt")
                }
            }
            if let url = Self.sourceURL {
                Section {
                    Link("Source code", destination: url)
                } footer: {
                    Text("Complete corresponding source of this version, with build instructions.")
                }
            }
        }
        .navigationTitle("Open-source licenses")
    }
}

private struct LicenseText: View {
    let title: LocalizedStringKey
    let resource: String
    let ext: String

    var body: some View {
        ScrollView {
            Text(Self.load(resource, ext))
                .font(.system(.footnote, design: .monospaced))
                .textSelection(.enabled)
                .frame(maxWidth: .infinity, alignment: .leading)
                .padding()
        }
        .navigationTitle(title)
    }

    static func load(_ name: String, _ ext: String) -> String {
        guard let url = Bundle.main.url(forResource: name, withExtension: ext),
              let s = try? String(contentsOf: url, encoding: .utf8) else { return "—" }
        return s
    }
}
