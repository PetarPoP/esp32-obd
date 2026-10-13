from reportlab.lib import colors
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import cm
from reportlab.platypus import Paragraph, SimpleDocTemplate, Spacer, Table, TableStyle, PageBreak


def build_styles():
    styles = getSampleStyleSheet()
    styles.add(
        ParagraphStyle(
            name="Body",
            parent=styles["Normal"],
            fontName="Helvetica",
            fontSize=11,
            leading=16,
            spaceAfter=8,
        )
    )
    styles.add(
        ParagraphStyle(
            name="H1Custom",
            parent=styles["Heading1"],
            fontName="Helvetica-Bold",
            fontSize=16,
            leading=20,
            spaceBefore=10,
            spaceAfter=10,
        )
    )
    styles.add(
        ParagraphStyle(
            name="H2Custom",
            parent=styles["Heading2"],
            fontName="Helvetica-Bold",
            fontSize=13,
            leading=17,
            spaceBefore=10,
            spaceAfter=8,
        )
    )
    styles.add(
        ParagraphStyle(
            name="CodeBlock",
            parent=styles["Normal"],
            fontName="Courier",
            fontSize=9,
            leading=12,
            leftIndent=10,
            backColor=colors.whitesmoke,
            borderWidth=0.5,
            borderColor=colors.lightgrey,
            borderPadding=6,
            spaceAfter=10,
        )
    )
    styles.add(
        ParagraphStyle(
            name="CenterBody",
            parent=styles["Body"],
            alignment=1,
        )
    )
    return styles


def build_title_page(story, styles):
    story.append(Spacer(1, 2.6 * cm))
    story.append(Paragraph("SVEUČILIŠTE U SPLITU", styles["CenterBody"]))
    story.append(Paragraph("SVEUČILIŠNI ODJEL ZA STRUČNE STUDIJE", styles["CenterBody"]))
    story.append(Paragraph("Odsjek za računarstvo", styles["CenterBody"]))
    story.append(Spacer(1, 2.5 * cm))
    story.append(Paragraph("<b>PETAR</b>", styles["CenterBody"]))
    story.append(Spacer(1, 1.2 * cm))
    story.append(Paragraph("<b>SEMINARSKI RAD</b>", styles["CenterBody"]))
    story.append(Spacer(1, 1.0 * cm))
    story.append(
        Paragraph(
            "<b>ESP32 OBD-II/BMS dijagnostički firmware s BLE vezom</b>",
            styles["CenterBody"],
        )
    )
    story.append(Spacer(1, 7.5 * cm))
    story.append(Paragraph("Split, svibanj, 2026.", styles["CenterBody"]))
    story.append(PageBreak())


def section(story, styles, title):
    story.append(Paragraph(title, styles["H1Custom"]))


def subsection(story, styles, title):
    story.append(Paragraph(title, styles["H2Custom"]))


def para(story, styles, text):
    story.append(Paragraph(text, styles["Body"]))


def code(story, styles, text):
    safe = text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace("\n", "<br/>")
    story.append(Paragraph(safe, styles["CodeBlock"]))


def add_footer(canvas, doc):
    page_num = canvas.getPageNumber()
    canvas.saveState()
    canvas.setFont("Helvetica", 9)
    canvas.drawCentredString(A4[0] / 2.0, 1.0 * cm, f"-- {page_num} --")
    canvas.restoreState()


def build_document(path):
    styles = build_styles()
    doc = SimpleDocTemplate(
        path,
        pagesize=A4,
        rightMargin=2.2 * cm,
        leftMargin=2.2 * cm,
        topMargin=2.0 * cm,
        bottomMargin=2.0 * cm,
        title="Seminarski rad - ESP32 OBD",
        author="Petar",
    )
    story = []

    build_title_page(story, styles)

    section(story, styles, "Sažetak")
    para(
        story,
        styles,
        "U ovom seminarskom radu opisana je izrada firmware sustava za dijagnostiku vozila na platformi ESP32. "
        "Projekt je fokusiran na pasivno i sigurno čitanje podataka s CAN sabirnice koristeći ESP32-ov TWAI kontroler "
        "s fizičkim CAN shieldom. Aplikacijska veza prema klijentu realizirana je preko BLE protokola i GATT modela "
        "s tri karakteristike: telemetrija, naredbe i odgovori.",
    )
    para(
        story,
        styles,
        "Arhitektura je modularna i podijeljena na drivere, OBD transport, dekodere te servisni sloj. "
        "Sustav podržava dva dijagnostička profila: standardni OBD Mode 01 za osnovne PID metrike i Smart ED profil "
        "za EV/BMS metrike, uključujući SoC, HV napon/struju, temperature i statistiku ćelija. U početnom stanju firmware "
        "se podiže u listen-only modu i time osigurava read-only rad dok klijent eksplicitno ne zatraži promjenu veze.",
    )

    section(story, styles, "1. Korištene tehnologije")
    subsection(story, styles, "1.1. ESP32 i TWAI")
    para(
        story,
        styles,
        "Projekt koristi ESP32 mikrokontroler s ugrađenim TWAI (CAN) kontrolerom. "
        "CAN komunikacija je mapirana na pinove GPIO4 (RX) i GPIO5 (TX), u skladu s hardverskim zahtjevima MrDIY ESP32 CAN Bus Shielda.",
    )
    table_data = [
        ["Kategorija", "Vrijednost u projektu"],
        ["Mikrokontroler", "ESP32 (Arduino framework)"],
        ["CAN kontroler", "TWAI (driver/twai.h)"],
        ["CAN pinovi", "RX = GPIO4, TX = GPIO5"],
        ["Podržane brzine", "100, 125, 250, 500, 1000 kbps"],
        ["Sigurnosni default", "listen-only, read-only"],
    ]
    table = Table(table_data, colWidths=[5.2 * cm, 9.8 * cm])
    table.setStyle(
        TableStyle(
            [
                ("BACKGROUND", (0, 0), (-1, 0), colors.lightgrey),
                ("FONTNAME", (0, 0), (-1, 0), "Helvetica-Bold"),
                ("GRID", (0, 0), (-1, -1), 0.5, colors.grey),
                ("VALIGN", (0, 0), (-1, -1), "TOP"),
                ("FONTSIZE", (0, 0), (-1, -1), 10),
            ]
        )
    )
    story.append(table)
    story.append(Spacer(1, 0.3 * cm))

    subsection(story, styles, "1.2. BLE GATT aplikacijska veza")
    para(
        story,
        styles,
        "Za komunikaciju s mobilnom aplikacijom koristi se BLE servis s vlastitim UUID-ovima. "
        "Telemetrija i odgovori šalju se kroz notify karakteristike, dok klijent šalje naredbe preko write karakteristike.",
    )
    code(
        story,
        styles,
        'Service UUID: 6f0a0001-8f7f-4c0c-9c2c-44d4d818a001\n'
        "telemetry_notify: 6f0a0002-8f7f-4c0c-9c2c-44d4d818a001\n"
        "command_write:    6f0a0003-8f7f-4c0c-9c2c-44d4d818a001\n"
        "response_notify:  6f0a0004-8f7f-4c0c-9c2c-44d4d818a001",
    )

    subsection(story, styles, "1.3. OBD i UDS pristup")
    para(
        story,
        styles,
        "U OBD Mode 01 profilu firmware šalje funkcionalne zahtjeve (ID 0x7DF) i prima ECU odgovore (0x7E8-0x7EF). "
        "Za Smart ED profil koristi se hibridni pristup: pasivno slušanje broadcast frameova uz periodične UDS upite za detaljne BMS podatke.",
    )
    subsection(story, styles, "1.4. Korištene komponente i alati")
    used_data = [
        ["Stavka", "Korišteno u projektu"],
        ["Mikrokontroler", "ESP-32D razvojna pločica"],
        ["CAN dodatak", "MrDIY ESP32 CAN Bus Shield v1.3b"],
        ["Komunikacija prema aplikaciji", "BLE (GATT servis + 3 karakteristike)"],
        ["Firmware okruženje", "Arduino framework (C++)"],
        ["CAN driver", "ESP32 TWAI (`driver/twai.h`)"],
        ["OBD sloj", "OBD Mode 01 + Smart ED UDS/BMS logika"],
        ["Repo struktura", "`firmware/esp32-obd` + `docs/protocol.md`"],
    ]
    used_table = Table(used_data, colWidths=[5.5 * cm, 9.5 * cm])
    used_table.setStyle(
        TableStyle(
            [
                ("BACKGROUND", (0, 0), (-1, 0), colors.lightgrey),
                ("FONTNAME", (0, 0), (-1, 0), "Helvetica-Bold"),
                ("GRID", (0, 0), (-1, -1), 0.5, colors.grey),
                ("VALIGN", (0, 0), (-1, -1), "TOP"),
                ("FONTSIZE", (0, 0), (-1, -1), 10),
            ]
        )
    )
    story.append(used_table)
    story.append(Spacer(1, 0.2 * cm))

    section(story, styles, "2. Implementacija")
    subsection(story, styles, "2.1. Pokretanje sustava")
    para(
        story,
        styles,
        "Sustav se inicijalizira kroz jasne korake: podizanje sigurnosnog guarda, CAN PHY inicijalizacija, "
        "TWAI start u listen-only modu, pokretanje OBD transporta i BLE sloja te publikacija inicijalnog statusa i capability poruke.",
    )
    code(
        story,
        styles,
        "if (!g_twai.BeginAt(500, /*listen_only=*/true)) {\n"
        '  Serial.println("TWAI listen-only init failed");\n'
        "} else {\n"
        '  Serial.println("TWAI ready (listen-only, 500 kbps default; awaiting set_link)");\n'
        "}",
    )

    subsection(story, styles, "2.2. Obrada BLE naredbi")
    para(
        story,
        styles,
        "Podržane naredbe uključuju start/stop stream, read_dtc, probe_capabilities i set_link. "
        "Naredba clear_dtc je namjerno zaključana sigurnosnom politikom te vraća status locked.",
    )
    code(
        story,
        styles,
        '{ "command": "start_stream" }\n'
        '{ "command": "stop_stream" }\n'
        '{ "command": "read_dtc" }\n'
        '{ "command": "probe_capabilities" }\n'
        '{ "command": "set_link", "baud_kbps": 500, "mode": "listen_only", "stream_mode": "raw_frames" }',
    )

    subsection(story, styles, "2.3. Scheduler i stream modovi")
    para(
        story,
        styles,
        "ObdSchedulerService upravlja ritmom obrade i prebacivanjem između dva stream moda. "
        "Raw mode prosljeđuje CAN frameove, a telemetry mode rotacijski čita PID-ove ili Smart ED metrike, "
        "uz ograničenje broja frameova po ticku radi responzivnosti glavne petlje.",
    )
    para(
        story,
        styles,
        "Za Smart ED profil servis dekodira broadcast identifikatore (npr. 0x518, 0x508, 0x448) "
        "i dodatno izvršava UDS rotaciju upita za kapacitet, temperature i napone ćelija.",
    )

    subsection(story, styles, "2.4. Telemetrija i dekodiranje")
    para(
        story,
        styles,
        "Telemetrija se šalje kao JSON payload i sadrži status (ok/degraded), vremensku oznaku te raspoložive metrike. "
        "Ako u jednom ciklusu nije dohvaćen nijedan validan podatak, šalje se degraded paket radi lakšeg debugiranja "
        "stanja sabirnice i dijagnostičkog profila.",
    )
    code(
        story,
        styles,
        '{\n'
        '  "type":"telemetry",\n'
        '  "status":"ok",\n'
        '  "soc_pct":74.5,\n'
        '  "hv_v":355.8,\n'
        '  "hv_amps":-21.3,\n'
        '  "temp_mean_c":28.4,\n'
        '  "cell_mv_stddev":9.7,\n'
        '  "ts":1234567\n'
        "}",
    )

    section(story, styles, "3. Sigurnost i ograničenja")
    para(
        story,
        styles,
        "Projekt je namjerno postavljen na sigurnu dijagnostiku bez destruktivnih write operacija. "
        "TWAI driver ne dopušta slanje frameova kada je aktivan listen-only režim, a clear_dtc ostaje zaključan "
        "preko SafetyGuard komponente.",
    )
    para(
        story,
        styles,
        "U OBD transportu trenutno su implementirani samo single-frame ISO-TP odgovori. "
        "Multi-frame OBD odgovori nisu podržani u ovoj iteraciji, dok su multi-frame UDS odgovori implementirani "
        "u SmartBmsService sloju za specifične DID upite.",
    )

    section(story, styles, "4. Hardversko spajanje")
    para(
        story,
        styles,
        "Ciljna konfiguracija koristi ESP-32D s MrDIY ESP32 CAN Bus Shield v1.3b. "
        "Pravilno mapiranje pinova je ključno za stabilan RX/TX rad i kompatibilnost s TWAI driverom.",
    )
    hw_table_data = [
        ["ESP32 pin", "Namjena", "Napomena"],
        ["GPIO4", "TWAI RX", "CAN RX prema transceiveru"],
        ["GPIO5", "TWAI TX", "CAN TX prema transceiveru"],
        ["GND", "Masa", "Zajednička referenca"],
        ["5V/USB", "Napajanje", "Ovisno o pločici/shieldu"],
    ]
    hw_table = Table(hw_table_data, colWidths=[3.5 * cm, 4.5 * cm, 7.0 * cm])
    hw_table.setStyle(
        TableStyle(
            [
                ("BACKGROUND", (0, 0), (-1, 0), colors.lightgrey),
                ("FONTNAME", (0, 0), (-1, 0), "Helvetica-Bold"),
                ("GRID", (0, 0), (-1, -1), 0.5, colors.grey),
                ("FONTSIZE", (0, 0), (-1, -1), 10),
            ]
        )
    )
    story.append(hw_table)
    story.append(Spacer(1, 0.3 * cm))
    subsection(story, styles, "4.1. Shema spajanja (logički prikaz)")
    para(
        story,
        styles,
        "U nastavku je prikazana jednostavna shema spajanja za laboratorijsko korištenje. "
        "ESP32 i CAN shield su direktno spojeni, a shield je dalje spojen na OBD/CAN linije vozila.",
    )
    code(
        story,
        styles,
        "                +---------------------------+\n"
        "                |        Mobilna app        |\n"
        "                |      (BLE klijent)        |\n"
        "                +-------------+-------------+\n"
        "                              |\n"
        "                         BLE GATT\n"
        "                              |\n"
        "+-----------------------------v-----------------------------+\n"
        "|                      ESP32 (firmware)                    |\n"
        "|   TWAI RX: GPIO4                          TWAI TX: GPIO5 |\n"
        "+-----------------------------+-----------------------------+\n"
        "                              |\n"
        "                    +---------v----------+\n"
        "                    | MrDIY CAN Shield   |\n"
        "                    |  CAN transceiver   |\n"
        "                    +----+-----------+---+\n"
        "                         |           |\n"
        "                      CANH          CANL\n"
        "                         |           |\n"
        "                +--------v-----------v--------+\n"
        "                |      Vozilo / OBD mreža     |\n"
        "                +-----------------------------+",
    )
    para(
        story,
        styles,
        "Napomena: firmware se podiže u listen-only modu (read-only) kako bi početno spajanje bilo sigurno i nedestruktivno.",
    )

    section(story, styles, "5. Pokretanje i testiranje")
    subsection(story, styles, "5.1. Priprema")
    para(story, styles, "1) Instalirati ESP32 board paket i potrebne biblioteke za BLE/TWAI u Arduino okruženju.")
    para(story, styles, "2) Spojiti ESP32 + CAN shield i potvrditi da je serijski port dostupan.")
    para(story, styles, "3) Uploadati firmware iz `firmware/esp32-obd` i otvoriti Serial Monitor na 115200 baud.")
    para(story, styles, "4) U aplikaciji se spojiti na BLE uređaj naziva `OBD-ESP32`.")

    subsection(story, styles, "5.2. Testni scenariji")
    para(story, styles, "- Test 1: set_link s listen_only i 500 kbps -> očekuje se `ok:true` i status snapshot.")
    para(story, styles, "- Test 2: start_stream + raw_frames -> očekuje se kontinuirani `raw_frame` promet.")
    para(story, styles, "- Test 3: start_stream + telemetry -> očekuju se periodični telemetry JSON paketi.")
    para(story, styles, "- Test 4: read_dtc -> očekuje se odgovor tipa `dtc` s kodovima.")
    para(story, styles, "- Test 5: clear_dtc -> očekuje se `status: locked` zbog sigurnosne politike.")

    subsection(story, styles, "5.3. Uočeni rizici")
    para(
        story,
        styles,
        "Najveći operativni rizik je pogrešno odabrana brzina CAN sabirnice. "
        "Zato je inicijalni boot postavljen na pasivni listen-only pri 500 kbps, uz mogućnost promjene tek preko eksplicitnog `set_link` zahtjeva.",
    )

    section(story, styles, "Zaključak")
    para(
        story,
        styles,
        "Implementirani firmware pruža stabilan temelj za IoT dijagnostiku vozila na ESP32 platformi. "
        "Arhitektura je modularna, sigurnosno konzervativna i spremna za nadogradnje. "
        "Posebna vrijednost projekta je kombinacija standardnog OBD pristupa i Smart ED BMS telemetrije kroz jedinstveni BLE API.",
    )
    para(
        story,
        styles,
        "Daljnja proširenja mogu uključiti potvrdu podržanih PID-ova direktnim probingom vozila, "
        "naprednije reconnect mehanizme, bogatiji fault model i integraciju dodatnih EV profila.",
    )

    section(story, styles, "Literatura")
    refs = [
        "[1] Espressif Systems, ESP32 Series, https://www.espressif.com/.",
        "[2] Espressif IDF, TWAI (CAN) Driver Documentation.",
        "[3] Bluetooth SIG, GATT Specifications.",
        "[4] ISO 15765-4 (OBD over CAN), standard overview.",
        "[5] Projektna dokumentacija: docs/protocol.md.",
        "[6] Izvorni kod projekta: firmware/esp32-obd/*.",
    ]
    for ref in refs:
        para(story, styles, ref)

    doc.build(story, onFirstPage=add_footer, onLaterPages=add_footer)


if __name__ == "__main__":
    build_document("docs/Petar-seminar-ESP32-OBD.pdf")
    print("Generated: docs/Petar-seminar-ESP32-OBD.pdf")
