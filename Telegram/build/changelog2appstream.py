#!/usr/bin/env python3

import re
import datetime
import argparse

try:
    from xml.etree import ElementTree as ET
    have_et = True
except (ImportError, Exception):
    have_et = False

def parse_changelog(changelog_path):
    version_re = re.compile(r'([\d.-]+)\s+(\w+)?\s*\((\d{2}.\d{2}\.\d{2})\)')
    entry_re = re.compile(r'-\s(.*)')

    with open(changelog_path, "r", encoding="utf-8") as f:
        changelog_lines = f.read().splitlines()

    releases = []
    for l in changelog_lines:
        version_match = version_re.match(l)
        entry_match = entry_re.match(l)
        if version_match is not None:
            version, prerelease, date = version_match.groups()
            release = (version,
                       prerelease,
                       datetime.datetime.strptime(date, '%d.%m.%y').date(),
                       [])
            releases.append(release)
        elif entry_match is not None:
            release[3].append(entry_match.group(1))

    return releases

def get_release_xml(version, prerelease, date, changes):
    release = ET.Element("release")
    if prerelease is None:
        ver_str = version
    else:
        ver_str = f"{version}~{prerelease}"
    release.set("version", ver_str)
    release.set("date", date.isoformat())
    description = ET.SubElement(release, "description")
    changelist = ET.SubElement(description, "ul")
    for c in changes:
        change = ET.SubElement(changelist, "li")
        change.text = c
    return release

def get_changelog_xml(changelog, max_items=None):
    releases = ET.Element("releases")
    if max_items is not None:
        changelog = changelog[:max_items]
    for rel in changelog:
        release = get_release_xml(*rel)
        releases.append(release)
    return releases

def update_metadata(metadata_path, changelog, max_items=None):
    if have_et:
        try:
            metadata = ET.parse(metadata_path)
            root = metadata.getroot()
            releases = root.find("releases")
            if releases is not None:
                root.remove(releases)
            root.append(
                get_changelog_xml(changelog, max_items)
            )
            metadata.write(metadata_path, encoding="utf-8", xml_declaration=True)
            return
        except Exception:
            pass

    # Fallback without pyexpat
    with open(metadata_path, 'r', encoding='utf-8') as f:
        content = f.read()

    if max_items is not None:
        changelog = changelog[:max_items]

    releases_xml = ['  <releases>']
    for version, prerelease, date, changes in changelog:
        ver_str = version if prerelease is None else f"{version}~{prerelease}"
        date_str = date.isoformat()
        releases_xml.append(f'    <release version="{ver_str}" date="{date_str}">')
        releases_xml.append('      <description>')
        releases_xml.append('        <ul>')
        for c in changes:
            esc_c = c.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')
            releases_xml.append(f'          <li>{esc_c}</li>')
        releases_xml.append('        </ul>')
        releases_xml.append('      </description>')
        releases_xml.append('    </release>')
    releases_xml.append('  </releases>')
    releases_str = '\n'.join(releases_xml)

    if '<releases>' in content and '</releases>' in content:
        content = re.sub(r'<releases>.*?</releases>', releases_str, content, flags=re.DOTALL)
    elif '</component>' in content:
        content = content.replace('</component>', f'{releases_str}\n</component>')

    with open(metadata_path, 'w', encoding='utf-8') as f:
        f.write(content)

def main():
    ap = argparse.ArgumentParser("Parse Telegram changelog")
    ap.add_argument("-c", "--changelog-path", default="changelog.txt")
    ap.add_argument("-m", "--metadata-path", default="lib/xdg/org.telegram.desktop.metainfo.xml")
    ap.add_argument("-n", "--num-releases", type=int, default=None)
    args = ap.parse_args()
    update_metadata(args.metadata_path,
                   parse_changelog(args.changelog_path),
                   max_items=args.num_releases)

if __name__ == "__main__":
    main()
