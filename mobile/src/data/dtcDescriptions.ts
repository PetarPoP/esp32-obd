// Descriptions of generic (SAE J2012 / ISO 15031-6) trouble codes in English
// and Croatian. Systematic ranges (per cylinder, per bank/sensor) are generated;
// codes that are not listed fall back to a description of their code group, and
// manufacturer-specific codes (P1xxx, P30xx–P33xx, B1/B2, C1/C2, U1/U2) are
// labelled as such because their meaning depends on the make.

export type DtcLang = "hr" | "en";
type Text = { en: string; hr: string };

export type DtcDescription = {
  code: string;
  /** What the code means; for unknown codes the code group. */
  description: string;
  /** System the code belongs to, e.g. "Ignition system / misfire". */
  system: string;
  /** True when the code itself is in the table (not only its group). */
  known: boolean;
  manufacturerSpecific: boolean;
};

const T: Record<string, Text> = {};
const add = (code: string, en: string, hr: string) => {
  T[code] = { en, hr };
};
const p = (n: number) => `P${String(n).padStart(4, "0")}`;

// ---- Generated ranges -------------------------------------------------------

// Camshaft timing (P0010–P0025): bank 1 = P001x, bank 2 = P002x.
for (const [bank, base] of [[1, 10], [2, 20]] as const) {
  add(p(base), `Intake camshaft actuator circuit (bank ${bank})`, `Krug aktuatora usisnog bregastog vratila (red ${bank})`);
  add(p(base + 1), `Intake camshaft timing over-advanced or system performance (bank ${bank})`, `Usisno bregasto vratilo previše pomaknuto naprijed ili loš rad sustava (red ${bank})`);
  add(p(base + 2), `Intake camshaft timing over-retarded (bank ${bank})`, `Usisno bregasto vratilo previše zakašnjelo (red ${bank})`);
  add(p(base + 3), `Exhaust camshaft actuator circuit (bank ${bank})`, `Krug aktuatora ispušnog bregastog vratila (red ${bank})`);
  add(p(base + 4), `Exhaust camshaft timing over-advanced or system performance (bank ${bank})`, `Ispušno bregasto vratilo previše pomaknuto naprijed ili loš rad sustava (red ${bank})`);
  add(p(base + 5), `Exhaust camshaft timing over-retarded (bank ${bank})`, `Ispušno bregasto vratilo previše zakašnjelo (red ${bank})`);
}
add("P0016", "Crankshaft / camshaft position correlation (bank 1 sensor A)", "Neusklađenost koljenastog i bregastog vratila (red 1, senzor A)");
add("P0017", "Crankshaft / camshaft position correlation (bank 1 sensor B)", "Neusklađenost koljenastog i bregastog vratila (red 1, senzor B)");
add("P0018", "Crankshaft / camshaft position correlation (bank 2 sensor A)", "Neusklađenost koljenastog i bregastog vratila (red 2, senzor A)");
add("P0019", "Crankshaft / camshaft position correlation (bank 2 sensor B)", "Neusklađenost koljenastog i bregastog vratila (red 2, senzor B)");

// O2 sensor heater control circuits P0030–P0064.
const heaterBase: [number, number, number][] = [
  [30, 1, 1], [36, 1, 2], [42, 1, 3], [50, 2, 1], [56, 2, 2], [62, 2, 3],
];
for (const [base, bank, sensor] of heaterBase) {
  const where = { en: `bank ${bank} sensor ${sensor}`, hr: `red ${bank}, senzor ${sensor}` };
  add(p(base), `O2 sensor heater control circuit (${where.en})`, `Krug upravljanja grijačem lambda sonde (${where.hr})`);
  add(p(base + 1), `O2 sensor heater control circuit low (${where.en})`, `Krug grijača lambda sonde – nizak signal (${where.hr})`);
  add(p(base + 2), `O2 sensor heater control circuit high (${where.en})`, `Krug grijača lambda sonde – visok signal (${where.hr})`);
}

// O2 sensors P0130–P0167: six codes per sensor.
const o2Base: [number, number, number][] = [
  [130, 1, 1], [136, 1, 2], [142, 1, 3], [150, 2, 1], [156, 2, 2], [162, 2, 3],
];
const o2Kinds: Text[] = [
  { en: "circuit malfunction", hr: "kvar kruga" },
  { en: "circuit low voltage", hr: "nizak napon kruga" },
  { en: "circuit high voltage", hr: "visok napon kruga" },
  { en: "slow response", hr: "spor odziv" },
  { en: "no activity detected", hr: "nema aktivnosti" },
  { en: "heater circuit malfunction", hr: "kvar kruga grijača" },
];
for (const [base, bank, sensor] of o2Base) {
  o2Kinds.forEach((kind, i) =>
    add(p(base + i), `O2 sensor ${kind.en} (bank ${bank} sensor ${sensor})`, `Lambda sonda – ${kind.hr} (red ${bank}, senzor ${sensor})`)
  );
}

// Injectors.
add("P0200", "Injector circuit malfunction", "Kvar kruga brizgaljki");
for (let c = 1; c <= 12; c++) {
  add(p(200 + c), `Injector circuit malfunction, cylinder ${c}`, `Kvar kruga brizgaljke, cilindar ${c}`);
  const base = 261 + (c - 1) * 3;
  add(p(base), `Cylinder ${c} injector circuit low`, `Brizgaljka cilindra ${c} – nizak signal kruga`);
  add(p(base + 1), `Cylinder ${c} injector circuit high`, `Brizgaljka cilindra ${c} – visok signal kruga`);
  add(p(base + 2), `Cylinder ${c} contribution / balance fault`, `Cilindar ${c} – neujednačen doprinos (balans)`);
}

// Misfire and ignition coils.
add("P0300", "Random / multiple cylinder misfire detected", "Otkriveno preskakanje paljenja na više cilindara (nasumično)");
for (let c = 1; c <= 12; c++) {
  add(p(300 + c), `Cylinder ${c} misfire detected`, `Otkriveno preskakanje paljenja, cilindar ${c}`);
  add(p(350 + c), `Ignition coil ${String.fromCharCode(64 + c)} primary/secondary circuit (cylinder ${c})`, `Primarni/sekundarni krug bobine ${String.fromCharCode(64 + c)} (cilindar ${c})`);
}
add("P0350", "Ignition coil primary/secondary circuit", "Primarni/sekundarni krug bobine (zavojnice paljenja)");

// Glow plugs (diesel).
add("P0670", "Glow plug control module circuit", "Krug upravljačkog modula žarnica");
for (let c = 1; c <= 8; c++) {
  add(p(670 + c), `Cylinder ${c} glow plug circuit`, `Krug žarnice cilindra ${c}`);
}

// Wheel speed sensors (generic chassis codes).
const wheels: [string, Text][] = [
  ["C0035", { en: "left front", hr: "prednji lijevi" }],
  ["C0040", { en: "right front", hr: "prednji desni" }],
  ["C0045", { en: "left rear", hr: "stražnji lijevi" }],
  ["C0050", { en: "right rear", hr: "stražnji desni" }],
];
for (const [code, w] of wheels) {
  add(code, `Wheel speed sensor circuit, ${w.en}`, `Krug senzora brzine kotača, ${w.hr}`);
}

// Shift solenoids A–E (P0750–P0774).
"ABCDE".split("").forEach((s, i) => {
  const base = 750 + i * 5;
  add(p(base), `Shift solenoid ${s} malfunction`, `Kvar elektromagnetskog ventila mijenjanja ${s}`);
  add(p(base + 1), `Shift solenoid ${s} performance or stuck off`, `Ventil mijenjanja ${s} – loš rad ili zaglavljen isključen`);
  add(p(base + 2), `Shift solenoid ${s} stuck on`, `Ventil mijenjanja ${s} – zaglavljen uključen`);
  add(p(base + 3), `Shift solenoid ${s} electrical`, `Ventil mijenjanja ${s} – električni kvar`);
  add(p(base + 4), `Shift solenoid ${s} intermittent`, `Ventil mijenjanja ${s} – povremeni kvar`);
});

// Gear ratio P0730–P0736.
add("P0730", "Incorrect gear ratio", "Pogrešan prijenosni omjer");
["first", "second", "third", "fourth", "fifth", "reverse"].forEach((g, i) => {
  const hr = ["prvoj", "drugoj", "trećoj", "četvrtoj", "petoj", "hodu unatrag"][i];
  add(p(731 + i), `Gear ratio error in ${g} gear`, `Pogrešan prijenosni omjer u ${hr}${i < 5 ? " brzini" : ""}`);
});

// ---- Individual codes ---------------------------------------------------------

const LIST: [string, string, string][] = [
  // Fuel and air metering
  ["P0068", "MAP/MAF – throttle position correlation", "Neusklađenost MAP/MAF senzora i položaja leptira"],
  ["P0069", "MAP – barometric pressure correlation", "Neusklađenost tlaka usisne grane i atmosferskog tlaka"],
  ["P0087", "Fuel rail / system pressure too low", "Tlak goriva u railu / sustavu prenizak"],
  ["P0088", "Fuel rail / system pressure too high", "Tlak goriva u railu / sustavu previsok"],
  ["P0089", "Fuel pressure regulator performance", "Loš rad regulatora tlaka goriva"],
  ["P0090", "Fuel pressure regulator control circuit", "Krug upravljanja regulatorom tlaka goriva"],
  ["P0093", "Fuel system large leak detected", "Otkriveno veliko curenje u sustavu goriva"],
  ["P0100", "Mass air flow (MAF) circuit malfunction", "Kvar kruga senzora protoka zraka (MAF)"],
  ["P0101", "Mass air flow (MAF) circuit range / performance", "Senzor protoka zraka (MAF) – vrijednost izvan raspona"],
  ["P0102", "Mass air flow (MAF) circuit low input", "Senzor protoka zraka (MAF) – nizak signal"],
  ["P0103", "Mass air flow (MAF) circuit high input", "Senzor protoka zraka (MAF) – visok signal"],
  ["P0104", "Mass air flow (MAF) circuit intermittent", "Senzor protoka zraka (MAF) – povremeni kvar"],
  ["P0105", "Manifold pressure (MAP) circuit malfunction", "Kvar kruga senzora tlaka usisne grane (MAP)"],
  ["P0106", "Manifold pressure (MAP) circuit range / performance", "Senzor tlaka usisne grane (MAP) – vrijednost izvan raspona"],
  ["P0107", "Manifold pressure (MAP) circuit low input", "Senzor tlaka usisne grane (MAP) – nizak signal"],
  ["P0108", "Manifold pressure (MAP) circuit high input", "Senzor tlaka usisne grane (MAP) – visok signal"],
  ["P0110", "Intake air temperature circuit malfunction", "Kvar kruga senzora temperature usisnog zraka"],
  ["P0111", "Intake air temperature circuit range / performance", "Senzor temperature usisnog zraka – vrijednost izvan raspona"],
  ["P0112", "Intake air temperature circuit low input", "Senzor temperature usisnog zraka – nizak signal"],
  ["P0113", "Intake air temperature circuit high input", "Senzor temperature usisnog zraka – visok signal"],
  ["P0115", "Engine coolant temperature circuit malfunction", "Kvar kruga senzora temperature rashladne tekućine"],
  ["P0116", "Engine coolant temperature circuit range / performance", "Senzor temperature rashladne tekućine – vrijednost izvan raspona"],
  ["P0117", "Engine coolant temperature circuit low input", "Senzor temperature rashladne tekućine – nizak signal"],
  ["P0118", "Engine coolant temperature circuit high input", "Senzor temperature rashladne tekućine – visok signal"],
  ["P0119", "Engine coolant temperature circuit intermittent", "Senzor temperature rashladne tekućine – povremeni kvar"],
  ["P0120", "Throttle position sensor A circuit malfunction", "Kvar kruga senzora položaja leptira A"],
  ["P0121", "Throttle position sensor A range / performance", "Senzor položaja leptira A – vrijednost izvan raspona"],
  ["P0122", "Throttle position sensor A circuit low input", "Senzor položaja leptira A – nizak signal"],
  ["P0123", "Throttle position sensor A circuit high input", "Senzor položaja leptira A – visok signal"],
  ["P0125", "Coolant temperature too low for closed-loop fuel control", "Rashladna tekućina prehladna za regulaciju smjese (zatvorena petlja)"],
  ["P0128", "Coolant temperature below thermostat regulating temperature", "Temperatura rashladne tekućine ispod radne temperature termostata"],
  ["P0170", "Fuel trim malfunction (bank 1)", "Kvar korekcije smjese goriva (red 1)"],
  ["P0171", "System too lean (bank 1)", "Smjesa presiromašna (red 1)"],
  ["P0172", "System too rich (bank 1)", "Smjesa prebogata (red 1)"],
  ["P0173", "Fuel trim malfunction (bank 2)", "Kvar korekcije smjese goriva (red 2)"],
  ["P0174", "System too lean (bank 2)", "Smjesa presiromašna (red 2)"],
  ["P0175", "System too rich (bank 2)", "Smjesa prebogata (red 2)"],
  ["P0180", "Fuel temperature sensor A circuit", "Krug senzora temperature goriva A"],
  ["P0190", "Fuel rail pressure sensor circuit", "Krug senzora tlaka goriva u railu"],
  ["P0191", "Fuel rail pressure sensor range / performance", "Senzor tlaka goriva u railu – vrijednost izvan raspona"],
  ["P0192", "Fuel rail pressure sensor circuit low", "Senzor tlaka goriva u railu – nizak signal"],
  ["P0193", "Fuel rail pressure sensor circuit high", "Senzor tlaka goriva u railu – visok signal"],
  ["P0217", "Engine overheat condition", "Pregrijavanje motora"],
  ["P0218", "Transmission fluid overheat condition", "Pregrijavanje ulja mjenjača"],
  ["P0219", "Engine overspeed condition", "Prekoračen dopušteni broj okretaja motora"],
  ["P0220", "Throttle position sensor B circuit malfunction", "Kvar kruga senzora položaja leptira B"],
  ["P0221", "Throttle position sensor B range / performance", "Senzor položaja leptira B – vrijednost izvan raspona"],
  ["P0222", "Throttle position sensor B circuit low input", "Senzor položaja leptira B – nizak signal"],
  ["P0223", "Throttle position sensor B circuit high input", "Senzor položaja leptira B – visok signal"],
  ["P0230", "Fuel pump primary circuit", "Primarni krug pumpe goriva"],
  ["P0234", "Turbo / supercharger overboost", "Previsok tlak punjenja turbopunjača"],
  ["P0235", "Turbo boost sensor A circuit", "Krug senzora tlaka punjenja A"],
  ["P0236", "Turbo boost sensor A range / performance", "Senzor tlaka punjenja A – vrijednost izvan raspona"],
  ["P0237", "Turbo boost sensor A circuit low", "Senzor tlaka punjenja A – nizak signal"],
  ["P0238", "Turbo boost sensor A circuit high", "Senzor tlaka punjenja A – visok signal"],
  ["P0243", "Turbo wastegate solenoid A", "Elektromagnetski ventil wastegatea A"],
  ["P0299", "Turbo / supercharger underboost", "Prenizak tlak punjenja turbopunjača"],
  // Ignition
  ["P0320", "Ignition / distributor engine speed input circuit", "Krug ulaznog signala broja okretaja (paljenje)"],
  ["P0325", "Knock sensor 1 circuit (bank 1)", "Krug senzora detonacije 1 (red 1)"],
  ["P0326", "Knock sensor 1 range / performance (bank 1)", "Senzor detonacije 1 – vrijednost izvan raspona (red 1)"],
  ["P0327", "Knock sensor 1 circuit low (bank 1)", "Senzor detonacije 1 – nizak signal (red 1)"],
  ["P0328", "Knock sensor 1 circuit high (bank 1)", "Senzor detonacije 1 – visok signal (red 1)"],
  ["P0330", "Knock sensor 2 circuit (bank 2)", "Krug senzora detonacije 2 (red 2)"],
  ["P0332", "Knock sensor 2 circuit low (bank 2)", "Senzor detonacije 2 – nizak signal (red 2)"],
  ["P0333", "Knock sensor 2 circuit high (bank 2)", "Senzor detonacije 2 – visok signal (red 2)"],
  ["P0335", "Crankshaft position sensor A circuit", "Krug senzora položaja koljenastog vratila A"],
  ["P0336", "Crankshaft position sensor A range / performance", "Senzor položaja koljenastog vratila A – vrijednost izvan raspona"],
  ["P0337", "Crankshaft position sensor A circuit low", "Senzor položaja koljenastog vratila A – nizak signal"],
  ["P0338", "Crankshaft position sensor A circuit high", "Senzor položaja koljenastog vratila A – visok signal"],
  ["P0339", "Crankshaft position sensor A intermittent", "Senzor položaja koljenastog vratila A – povremeni kvar"],
  ["P0340", "Camshaft position sensor A circuit (bank 1)", "Krug senzora položaja bregastog vratila A (red 1)"],
  ["P0341", "Camshaft position sensor A range / performance (bank 1)", "Senzor položaja bregastog vratila A – vrijednost izvan raspona (red 1)"],
  ["P0342", "Camshaft position sensor A circuit low (bank 1)", "Senzor položaja bregastog vratila A – nizak signal (red 1)"],
  ["P0343", "Camshaft position sensor A circuit high (bank 1)", "Senzor položaja bregastog vratila A – visok signal (red 1)"],
  ["P0345", "Camshaft position sensor A circuit (bank 2)", "Krug senzora položaja bregastog vratila A (red 2)"],
  ["P0380", "Glow plug / heater circuit A", "Krug žarnica / grijača A"],
  ["P0381", "Glow plug / heater indicator circuit", "Krug žaruljice žarnica"],
  // Auxiliary emission controls
  ["P0400", "EGR flow malfunction", "Kvar protoka recirkulacije ispušnih plinova (EGR)"],
  ["P0401", "EGR flow insufficient", "Nedovoljan protok EGR-a"],
  ["P0402", "EGR flow excessive", "Prevelik protok EGR-a"],
  ["P0403", "EGR control circuit", "Krug upravljanja EGR ventilom"],
  ["P0404", "EGR control circuit range / performance", "Upravljanje EGR ventilom – vrijednost izvan raspona"],
  ["P0405", "EGR sensor A circuit low", "Senzor EGR-a A – nizak signal"],
  ["P0406", "EGR sensor A circuit high", "Senzor EGR-a A – visok signal"],
  ["P0410", "Secondary air injection system", "Sustav sekundarnog zraka"],
  ["P0420", "Catalyst efficiency below threshold (bank 1)", "Učinkovitost katalizatora ispod praga (red 1)"],
  ["P0421", "Warm-up catalyst efficiency below threshold (bank 1)", "Učinkovitost predkatalizatora ispod praga (red 1)"],
  ["P0430", "Catalyst efficiency below threshold (bank 2)", "Učinkovitost katalizatora ispod praga (red 2)"],
  ["P0440", "Evaporative emission (EVAP) system malfunction", "Kvar sustava povrata para goriva (EVAP)"],
  ["P0441", "EVAP system incorrect purge flow", "EVAP – pogrešan protok pročišćavanja"],
  ["P0442", "EVAP system small leak detected", "EVAP – otkriveno malo curenje"],
  ["P0443", "EVAP purge control valve circuit", "Krug ventila za pročišćavanje EVAP sustava"],
  ["P0446", "EVAP vent control circuit", "Krug odzračnog ventila EVAP sustava"],
  ["P0449", "EVAP vent valve / solenoid circuit", "Krug odzračnog ventila EVAP sustava"],
  ["P0451", "EVAP pressure sensor range / performance", "Senzor tlaka EVAP – vrijednost izvan raspona"],
  ["P0452", "EVAP pressure sensor low", "Senzor tlaka EVAP – nizak signal"],
  ["P0453", "EVAP pressure sensor high", "Senzor tlaka EVAP – visok signal"],
  ["P0455", "EVAP system large leak detected", "EVAP – otkriveno veliko curenje (često čep spremnika)"],
  ["P0456", "EVAP system very small leak detected", "EVAP – otkriveno vrlo malo curenje"],
  ["P0457", "EVAP leak detected (fuel cap loose / off)", "EVAP – curenje (čep spremnika labav ili otvoren)"],
  ["P0460", "Fuel level sensor circuit", "Krug senzora razine goriva"],
  ["P0480", "Cooling fan 1 control circuit", "Krug upravljanja ventilatorom hlađenja 1"],
  ["P0481", "Cooling fan 2 control circuit", "Krug upravljanja ventilatorom hlađenja 2"],
  // Speed, idle, auxiliary inputs
  ["P0500", "Vehicle speed sensor malfunction", "Kvar senzora brzine vozila"],
  ["P0501", "Vehicle speed sensor range / performance", "Senzor brzine vozila – vrijednost izvan raspona"],
  ["P0502", "Vehicle speed sensor circuit low", "Senzor brzine vozila – nizak signal"],
  ["P0503", "Vehicle speed sensor intermittent / erratic", "Senzor brzine vozila – povremen ili nestabilan signal"],
  ["P0505", "Idle control system malfunction", "Kvar regulacije praznog hoda"],
  ["P0506", "Idle speed lower than expected", "Prazni hod niži od očekivanog"],
  ["P0507", "Idle speed higher than expected", "Prazni hod viši od očekivanog"],
  ["P0513", "Invalid immobilizer key", "Neispravan ključ imobilajzera"],
  ["P0520", "Engine oil pressure sensor / switch circuit", "Krug senzora / prekidača tlaka ulja"],
  ["P0521", "Engine oil pressure sensor range / performance", "Senzor tlaka ulja – vrijednost izvan raspona"],
  ["P0522", "Engine oil pressure sensor low voltage", "Senzor tlaka ulja – nizak napon"],
  ["P0523", "Engine oil pressure sensor high voltage", "Senzor tlaka ulja – visok napon"],
  ["P0530", "A/C refrigerant pressure sensor circuit", "Krug senzora tlaka rashladnog sredstva klime"],
  ["P0532", "A/C refrigerant pressure sensor low", "Senzor tlaka klime – nizak signal"],
  ["P0533", "A/C refrigerant pressure sensor high", "Senzor tlaka klime – visok signal"],
  ["P0560", "System voltage malfunction", "Kvar napona sustava"],
  ["P0562", "System voltage low", "Napon sustava prenizak"],
  ["P0563", "System voltage high", "Napon sustava previsok"],
  ["P0571", "Brake switch A circuit", "Krug prekidača kočnice A"],
  // Computer and outputs
  ["P0600", "Serial communication link malfunction", "Kvar serijske komunikacije"],
  ["P0601", "Control module memory checksum error", "Greška kontrolnog zbroja memorije upravljačke jedinice"],
  ["P0602", "Control module programming error", "Greška programiranja upravljačke jedinice"],
  ["P0603", "Control module keep-alive memory (KAM) error", "Greška trajne memorije (KAM) upravljačke jedinice"],
  ["P0604", "Control module RAM error", "Greška RAM memorije upravljačke jedinice"],
  ["P0605", "Control module ROM error", "Greška ROM memorije upravljačke jedinice"],
  ["P0606", "Control module processor fault", "Kvar procesora upravljačke jedinice"],
  ["P0615", "Starter relay circuit", "Krug releja elektropokretača"],
  ["P0620", "Generator control circuit", "Krug upravljanja alternatorom"],
  ["P0627", "Fuel pump control circuit open", "Krug upravljanja pumpom goriva – prekid"],
  ["P0638", "Throttle actuator control range / performance (bank 1)", "Aktuator leptira – vrijednost izvan raspona (red 1)"],
  ["P0641", "Sensor reference voltage A circuit open", "Referentni napon senzora A – prekid"],
  ["P0650", "Malfunction indicator lamp (MIL) control circuit", "Krug žaruljice kvara motora (MIL)"],
  // Transmission
  ["P0700", "Transmission control system malfunction (see TCM codes)", "Kvar upravljanja mjenjačem (provjeri kodove mjenjača)"],
  ["P0705", "Transmission range sensor circuit (PRNDL)", "Krug senzora položaja ručice mjenjača (PRNDL)"],
  ["P0710", "Transmission fluid temperature sensor circuit", "Krug senzora temperature ulja mjenjača"],
  ["P0715", "Input / turbine speed sensor circuit", "Krug senzora brzine ulaznog vratila / turbine"],
  ["P0720", "Output speed sensor circuit", "Krug senzora brzine izlaznog vratila"],
  ["P0740", "Torque converter clutch circuit", "Krug spojke pretvarača momenta"],
  ["P0741", "Torque converter clutch performance or stuck off", "Spojka pretvarača momenta – loš rad ili zaglavljena isključena"],
  ["P0742", "Torque converter clutch stuck on", "Spojka pretvarača momenta – zaglavljena uključena"],
  ["P0841", "Transmission fluid pressure sensor / switch A range", "Senzor tlaka ulja mjenjača A – vrijednost izvan raspona"],
  ["P0868", "Transmission fluid pressure low", "Tlak ulja mjenjača prenizak"],
  // P2xxx
  ["P2002", "Diesel particulate filter efficiency below threshold (bank 1)", "Učinkovitost filtra čestica (DPF) ispod praga (red 1)"],
  ["P2004", "Intake manifold runner control stuck open (bank 1)", "Zaklopke usisne grane zaglavljene otvorene (red 1)"],
  ["P2008", "Intake manifold runner control circuit open (bank 1)", "Krug zaklopki usisne grane – prekid (red 1)"],
  ["P2015", "Intake manifold runner position sensor range / performance (bank 1)", "Senzor položaja zaklopki usisne grane – vrijednost izvan raspona (red 1)"],
  ["P2096", "Post-catalyst fuel trim system too lean (bank 1)", "Korekcija smjese iza katalizatora – presiromašno (red 1)"],
  ["P2097", "Post-catalyst fuel trim system too rich (bank 1)", "Korekcija smjese iza katalizatora – prebogato (red 1)"],
  ["P2101", "Throttle actuator control motor circuit range / performance", "Motor aktuatora leptira – vrijednost izvan raspona"],
  ["P2135", "Throttle / pedal position sensor A/B voltage correlation", "Neusklađen napon senzora položaja leptira / papučice A i B"],
  ["P2138", "Throttle / pedal position sensor D/E voltage correlation", "Neusklađen napon senzora položaja papučice D i E"],
  ["P2181", "Cooling system performance", "Loš rad sustava hlađenja"],
  ["P2187", "System too lean at idle (bank 1)", "Smjesa presiromašna u praznom hodu (red 1)"],
  ["P2188", "System too rich at idle (bank 1)", "Smjesa prebogata u praznom hodu (red 1)"],
  ["P2189", "System too lean at idle (bank 2)", "Smjesa presiromašna u praznom hodu (red 2)"],
  ["P2191", "System too lean at higher load (bank 1)", "Smjesa presiromašna pri većem opterećenju (red 1)"],
  ["P2195", "O2 sensor signal stuck lean (bank 1 sensor 1)", "Signal lambda sonde zaglavljen na siromašnom (red 1, senzor 1)"],
  ["P2196", "O2 sensor signal stuck rich (bank 1 sensor 1)", "Signal lambda sonde zaglavljen na bogatom (red 1, senzor 1)"],
  ["P2263", "Turbo / supercharger boost system performance", "Loš rad sustava prednabijanja (turbo)"],
  ["P2270", "O2 sensor signal stuck lean (bank 1 sensor 2)", "Signal lambda sonde zaglavljen na siromašnom (red 1, senzor 2)"],
  ["P2271", "O2 sensor signal stuck rich (bank 1 sensor 2)", "Signal lambda sonde zaglavljen na bogatom (red 1, senzor 2)"],
  ["P2279", "Intake air system leak", "Curenje u usisnom sustavu (fals zrak)"],
  ["P242F", "Diesel particulate filter restriction – ash accumulation", "Filtar čestica (DPF) začepljen – nakupljen pepeo"],
  ["P2452", "Diesel particulate filter pressure sensor A circuit", "Krug senzora tlaka filtra čestica (DPF) A"],
  ["P2453", "Diesel particulate filter pressure sensor A range / performance", "Senzor tlaka filtra čestica (DPF) A – vrijednost izvan raspona"],
  ["P2458", "Diesel particulate filter regeneration duration", "Predugo trajanje regeneracije filtra čestica (DPF)"],
  ["P2463", "Diesel particulate filter restriction – soot accumulation", "Filtar čestica (DPF) začepljen – nakupljena čađa"],
  ["P2563", "Turbo boost control position sensor range / performance", "Senzor položaja regulacije turba – vrijednost izvan raspona"],
  // Network
  ["U0001", "High-speed CAN communication bus", "Brza CAN sabirnica – kvar komunikacije"],
  ["U0073", "Control module communication bus A off", "Komunikacijska sabirnica A isključena (bus off)"],
  ["U0100", "Lost communication with ECM/PCM", "Izgubljena komunikacija s upravljačkom jedinicom motora (ECM/PCM)"],
  ["U0101", "Lost communication with TCM", "Izgubljena komunikacija s upravljačkom jedinicom mjenjača (TCM)"],
  ["U0121", "Lost communication with ABS control module", "Izgubljena komunikacija s ABS jedinicom"],
  ["U0140", "Lost communication with body control module", "Izgubljena komunikacija s jedinicom karoserije (BCM)"],
  ["U0151", "Lost communication with airbag control module", "Izgubljena komunikacija s jedinicom zračnih jastuka"],
  ["U0155", "Lost communication with instrument cluster", "Izgubljena komunikacija s instrument pločom"],
  ["U0164", "Lost communication with HVAC control module", "Izgubljena komunikacija s jedinicom klimatizacije"],
  ["U0401", "Invalid data received from ECM/PCM", "Neispravni podaci primljeni od jedinice motora (ECM/PCM)"],
];
for (const [code, en, hr] of LIST) add(code, en, hr);

// ---- Groups -------------------------------------------------------------------

const SYSTEMS: Record<string, Text> = {
  fuel_air: { en: "Fuel and air metering", hr: "Doziranje goriva i zraka" },
  fuel_air_aux: { en: "Fuel and air metering, auxiliary emission controls", hr: "Doziranje goriva i zraka, pomoćni sustavi emisije" },
  injector: { en: "Fuel and air metering (injector circuit)", hr: "Doziranje goriva i zraka (krug brizgaljki)" },
  ignition: { en: "Ignition system or misfire", hr: "Sustav paljenja ili preskakanje paljenja" },
  emission: { en: "Auxiliary emission controls", hr: "Pomoćni sustavi za smanjenje emisije" },
  speed_idle: { en: "Vehicle speed, idle control and auxiliary inputs", hr: "Brzina vozila, prazni hod i pomoćni ulazi" },
  computer: { en: "Control module and outputs", hr: "Upravljačka jedinica i izlazi" },
  transmission: { en: "Transmission", hr: "Mjenjač" },
  hybrid: { en: "Hybrid / electric propulsion", hr: "Hibridni / električni pogon" },
  cyl_deact: { en: "Cylinder deactivation", hr: "Isključivanje cilindara" },
  powertrain: { en: "Powertrain", hr: "Pogonski sklop" },
  body: { en: "Body", hr: "Karoserija" },
  chassis: { en: "Chassis", hr: "Šasija" },
  network: { en: "Network communication", hr: "Mrežna komunikacija" },
};

function groupOf(code: string): { system: keyof typeof SYSTEMS; manufacturer: boolean } {
  const letter = code[0];
  const d1 = code[1];
  const d2 = code[2];
  if (letter === "P") {
    if (d1 === "1" || (d1 === "3" && "0123".includes(d2))) return { system: "powertrain", manufacturer: true };
    if (d1 === "3") return { system: "cyl_deact", manufacturer: false };
    const byDigit: Record<string, keyof typeof SYSTEMS> =
      d1 === "0"
        ? { "0": "fuel_air_aux", "1": "fuel_air", "2": "injector", "3": "ignition", "4": "emission", "5": "speed_idle", "6": "computer", "7": "transmission", "8": "transmission", "9": "transmission", A: "hybrid", B: "hybrid", C: "hybrid" }
        : { "0": "fuel_air", "1": "fuel_air", "2": "fuel_air", "3": "ignition", "4": "emission", "5": "speed_idle", "6": "computer", "7": "transmission", "8": "transmission", "9": "transmission", A: "hybrid", B: "hybrid", C: "hybrid" };
    return { system: byDigit[d2] ?? "powertrain", manufacturer: false };
  }
  const system = letter === "B" ? "body" : letter === "C" ? "chassis" : "network";
  return { system, manufacturer: d1 === "1" || d1 === "2" };
}

/** Description of one trouble code (e.g. "P0301") in the given language. */
export function describeDtc(rawCode: string, lang: DtcLang): DtcDescription {
  const code = rawCode.trim().toUpperCase();
  const valid = /^[PBCU][0-3][0-9A-F]{3}$/.test(code);
  if (!valid) {
    return {
      code,
      description: lang === "hr" ? "Nepoznat format koda" : "Unknown code format",
      system: "",
      known: false,
      manufacturerSpecific: false,
    };
  }
  const group = groupOf(code);
  const system = SYSTEMS[group.system][lang];
  const entry = T[code];
  if (entry) {
    return { code, description: entry[lang], system, known: true, manufacturerSpecific: false };
  }
  const description = group.manufacturer
    ? lang === "hr"
      ? "Kod proizvođača – značenje ovisi o marki vozila"
      : "Manufacturer-specific code – the meaning depends on the make"
    : lang === "hr"
      ? `Generički kod: ${system.toLowerCase()}`
      : `Generic code: ${system.toLowerCase()}`;
  return { code, description, system, known: false, manufacturerSpecific: group.manufacturer };
}

/** Number of codes with their own description (for the settings screen). */
export const DTC_TABLE_SIZE = Object.keys(T).length;
