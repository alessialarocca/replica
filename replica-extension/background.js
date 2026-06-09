// ─────────────────────────────────────────────
// REPLICA_ - background service worker
// Passive data collection & profile generation
// ─────────────────────────────────────────────

const CATEGORIES = ['bio', 'geo', 'prof', 'econ', 'socio', 'psycho'];

// ─── Classifiers ──────────────────────────────
// Each category has:
//   keywords: matched against URL + page title
//   signals:  sub-signals that map to specific vocable slots
//             so the output reflects what was ACTUALLY found

const CLASSIFIERS = {

  bio: {
    keywords: [
      // health & body
      'health', 'medical', 'doctor', 'fitness', 'diet', 'gym', 'hospital',
      'pharmacy', 'symptom', 'disease', 'exercise', 'yoga', 'baby', 'pregnancy',
      'medication', 'wellness', 'nutrition', 'sleep', 'clinic', 'therapy',
      'dentist', 'pediatric', 'dermatol', 'cardio', 'mental health', 'anxiety',
      'depression', 'webmd', 'mayoclinic', 'healthline', 'myfitnesspal', 'strava',
      'garmin', 'runkeeper', 'fitbit', 'whoop', 'nhs.uk', 'humanitas', 'ospedale',
      // age-inference signals — younger
      'tiktok', 'twitch', 'discord', 'roblox', 'fortnite', 'minecraft',
      'university', 'college', 'campus', 'erasmus', 'studenti', 'univ.',
      'student loan', 'dorm', 'orientation', 'graduation', 'thesis', 'tesi',
      'first job', 'entry level', 'internship', 'tirocinio', 'stage',
      // age-inference signals — adult/working age
      'mortgage', 'mutuo', 'affitto', 'real estate', 'immobiliare',
      'insurance', 'assicurazione', 'tax', 'dichiarazione', 'fisco',
      'linkedin', 'career', 'promotion', 'salary', 'business travel',
      'wedding', 'matrimonio', 'engagement', 'honeymoon',
      // age-inference signals — parent
      'baby', 'pregnancy', 'gravidanza', 'pediatric', 'infant', 'toddler',
      'asilo', 'nido', 'scuola elementare', 'elementari', 'materna',
      'nanny', 'babysitter', 'puericultura', 'allattamento',
      // age-inference signals — older
      'pension', 'pensione', 'retirement', 'aging', 'elderly', 'senior',
      'cardiology', 'cardiolog', 'osteoporosi', 'osteoporosis',
      'colonscopia', 'colonoscopy', 'prostate', 'prostata',
    ],
    signals: {
      // life stage inference (primary for age)
      gen_z:      ['tiktok', 'twitch', 'discord', 'roblox', 'fortnite', 'minecraft', 'university', 'campus', 'dorm', 'erasmus', 'graduation', 'thesis', 'tesi', 'internship', 'tirocinio', 'first job', 'entry level'],
      millennial: ['mortgage', 'mutuo', 'affitto', 'insurance', 'assicurazione', 'wedding', 'matrimonio', 'career', 'promotion', 'linkedin', 'salary', 'tax'],
      parent:     ['baby', 'pregnancy', 'gravidanza', 'pediatric', 'infant', 'toddler', 'asilo', 'nido', 'elementari', 'nanny', 'allattamento'],
      mature:     ['pension', 'pensione', 'retirement', 'aging', 'elderly', 'senior', 'cardiology', 'osteoporosi', 'colonoscopy', 'prostate'],
      // body & health
      fitness:    ['gym', 'fitness', 'workout', 'exercise', 'strava', 'myfitnesspal', 'runkeeper', 'fitbit', 'whoop', 'yoga', 'pilates', 'crossfit'],
      health:     ['health', 'medical', 'doctor', 'hospital', 'clinic', 'pharmacy', 'symptom', 'disease', 'medication', 'webmd', 'mayoclinic', 'humanitas', 'ospedale'],
      wellness:   ['wellness', 'nutrition', 'diet', 'sleep', 'mental', 'anxiety', 'therapy', 'meditation', 'headspace', 'calm'],
    }
  },

  geo: {
    keywords: [
      'maps', 'directions', 'weather', 'forecast', 'local', 'near me', 'nearby',
      'restaurant', 'hotel', 'airbnb', 'booking', 'transport', 'metro', 'transit',
      'trip', 'travel', 'flight', 'train', 'bus', 'taxi', 'uber', 'waze',
      'tripadvisor', 'yelp', 'foursquare', 'google.com/maps', 'maps.apple',
      'accuweather', 'ilmeteo', 'meteo', 'atm', 'trenord', 'trenitalia',
      'ryanair', 'easyjet', 'alitalia', 'expedia', 'kayak', 'skyscanner',
      // Google location searches & map intent
      'google.com/search?q=where', 'google.com/search?q=how+to+get',
      'where is', 'how to get to', 'how far', 'distance from', 'distance to',
      'route to', 'driving route', 'street view', 'satellite view', 'address of', 'opening hours',
      'tickets to', 'fly to', 'train to', 'bus to', 'from milan', 'from rome',
      'from london', 'from paris', 'from new york',
      // common destination names (Google search for these usually = geo intent)
      'milan', 'milano', 'rome', 'roma', 'florence', 'firenze', 'venice', 'venezia',
      'naples', 'napoli', 'turin', 'torino', 'bologna', 'london', 'paris', 'berlin',
      'madrid', 'barcelona', 'amsterdam', 'lisbon', 'lisboa', 'vienna', 'zurich',
      'new york', 'los angeles', 'san francisco', 'tokyo', 'kyoto', 'osaka',
      'italy', 'italia', 'france', 'germany', 'spain', 'portugal', 'switzerland',
      'usa', 'united states', 'japan', 'uk', 'united kingdom'
    ],
    signals: {
      urban:      ['metro', 'atm', 'trenord', 'bus', 'tram', 'city', 'urban'],
      traveller:  ['flight', 'ryanair', 'easyjet', 'kayak', 'skyscanner', 'expedia', 'hotel', 'airbnb', 'booking', 'trip', 'tickets to', 'fly to', 'train to'],
      local:      ['near me', 'nearby', 'local', 'yelp', 'foursquare', 'restaurant', 'address of', 'opening hours'],
      commuter:   ['directions', 'transit', 'maps', 'waze', 'commute', 'trenitalia', 'uber', 'taxi', 'how to get to', 'route to', 'distance to'],
      weather:    ['weather', 'forecast', 'meteo', 'accuweather', 'ilmeteo'],
      searcher:   ['google.com/search?q=where', 'google.com/search?q=how+to+get', 'where is', 'street view', 'satellite'],
    }
  },

  prof: {
    keywords: [
      'linkedin', 'github', 'gitlab', 'bitbucket', 'stackoverflow', 'behance',
      'dribbble', 'figma', 'adobe', 'sketch', 'invision', 'zeplin', 'framer',
      'notion', 'slack', 'jira', 'confluence', 'asana', 'trello', 'monday.com',
      'resume', 'cv', 'portfolio', 'career', 'job', 'salary', 'hire', 'recruit',
      'freelance', 'university', 'course', 'tutorial', 'certification', 'degree',
      'research', 'paper', 'journal', 'pubmed', 'scholar', 'academia',
      'coursera', 'udemy', 'edx', 'masterclass', 'pluralsight',
      'zapier', 'airtable', 'salesforce', 'hubspot', 'workday'
    ],
    signals: {
      creative:   ['figma', 'adobe', 'sketch', 'behance', 'dribbble', 'invision', 'zeplin', 'framer', 'canva', 'procreate'],
      developer:  ['github', 'gitlab', 'stackoverflow', 'bitbucket', 'npm', 'docker', 'aws', 'vercel', 'heroku'],
      manager:    ['jira', 'confluence', 'asana', 'monday.com', 'salesforce', 'hubspot', 'notion', 'workday'],
      academic:   ['pubmed', 'scholar', 'academia', 'research', 'paper', 'journal', 'university', 'coursera', 'edx'],
      jobseeker:  ['linkedin', 'resume', 'cv', 'career', 'job', 'salary', 'hire', 'recruit', 'indeed', 'glassdoor'],
    }
  },

  econ: {
    keywords: [
      'amazon', 'ebay', 'zalando', 'zara', 'asos', 'shein', 'uniqlo', 'h&m',
      'paypal', 'revolut', 'n26', 'wise', 'stripe', 'klarna',
      'bank', 'fintech', 'price', 'shop', 'buy', 'sale', 'discount', 'offer',
      'mortgage', 'loan', 'credit', 'invest', 'bitcoin', 'crypto', 'stock', 'etf',
      'finance', 'budget', 'insurance', 'checkout', 'cart', 'deal', 'coupon',
      'trading', 'forex', 'binance', 'coinbase', 'degiro', 'trading212',
      'booking.com', 'agoda', 'ikea', 'mediaworld', 'euronics', 'apple store'
    ],
    signals: {
      shopper:    ['amazon', 'zalando', 'zara', 'asos', 'shein', 'ikea', 'ebay', 'cart', 'checkout', 'discount', 'sale'],
      banking:    ['bank', 'revolut', 'n26', 'paypal', 'wise', 'stripe', 'klarna', 'account', 'transfer'],
      investor:   ['stock', 'etf', 'bitcoin', 'crypto', 'invest', 'trading', 'forex', 'binance', 'degiro'],
      insured:    ['insurance', 'mortgage', 'loan', 'credit', 'fintech', 'budget'],
    }
  },

  socio: {
    keywords: [
      'twitter', 'x.com', 'facebook', 'instagram', 'tiktok', 'linkedin',
      'reddit', 'youtube', 'twitch', 'discord', 'telegram', 'whatsapp',
      'bbc', 'cnn', 'guardian', 'nytimes', 'repubblica', 'corriere', 'sole24ore',
      'news', 'politics', 'election', 'protest', 'vote', 'government', 'policy',
      'community', 'forum', 'petition', 'climate', 'environment', 'activism',
      'culture', 'art', 'museum', 'cinema', 'teatro', 'exhibition', 'galleria',
      'sport', 'football', 'calcio', 'gazzetta', 'skysport', 'dazn'
    ],
    signals: {
      news:       ['bbc', 'cnn', 'guardian', 'nytimes', 'repubblica', 'corriere', 'sole24ore', 'news', 'politics', 'election'],
      social:     ['twitter', 'x.com', 'facebook', 'instagram', 'tiktok', 'discord', 'telegram'],
      cultural:   ['art', 'museum', 'cinema', 'teatro', 'exhibition', 'galleria', 'culture', 'spotify', 'soundcloud'],
      civic:      ['petition', 'climate', 'environment', 'activism', 'protest', 'vote', 'government', 'policy'],
      sports:     ['sport', 'football', 'calcio', 'gazzetta', 'skysport', 'dazn', 'nba', 'serie a'],
    }
  },

  psycho: {
    keywords: [
      'spotify', 'netflix', 'disney', 'primevideo', 'hulu', 'appletv',
      'steam', 'gaming', 'twitch', 'ign', 'gamespot', 'epicgames', 'playstation', 'xbox',
      'meditation', 'headspace', 'calm', 'insight timer',
      'todoist', 'notion', 'obsidian', 'roamresearch', 'logseq',
      'calendar', 'google.com/calendar', 'fantastical', 'habits',
      'chatgpt', 'claude', 'openai', 'anthropic', 'gemini', 'copilot',
      'wikipedia', 'quora', 'medium', 'substack', 'pocket', 'instapaper',
      'duolingo', 'anki', 'goodreads', 'kindle', 'libby',
      'pornhub', 'onlyfans', 'dating', 'tinder', 'bumble', 'hinge'
    ],
    signals: {
      entertained: ['netflix', 'disney', 'primevideo', 'spotify', 'twitch', 'youtube', 'hulu', 'appletv'],
      gamer:       ['steam', 'gaming', 'ign', 'epicgames', 'playstation', 'xbox', 'gamespot'],
      organised:   ['todoist', 'notion', 'calendar', 'habits', 'fantastical', 'obsidian', 'logseq'],
      reader:      ['wikipedia', 'medium', 'substack', 'pocket', 'goodreads', 'kindle', 'quora'],
      ai_user:     ['chatgpt', 'claude', 'openai', 'anthropic', 'gemini', 'copilot', 'perplexity'],
      learner:     ['duolingo', 'anki', 'coursera', 'udemy', 'edx', 'masterclass'],
    }
  }

};

// ─── Vocable system ───────────────────────────
// Vocables are DERIVED from the actual signals collected,
// not pre-assumed. Each category has:
//   - tier1/2/3: confidence tiers (few/some/many data points)
//   - per signal slot: what label to use when that signal dominates
//   - poisoned/amplified: override states
//
// computeVocable() reads state.topSignal (set during classification)
// and picks the appropriate label.

// ─── Vocable map ───────────────────────────────────────
// Sentence template:
//   "IDENTIFIED AS [bio], WORKING AS [prof], LOCATED IN [geo],
//    NETWORKED WITHIN [socio], VALUED AS [econ],
//    AND EXHIBITING [psycho]."
//
// Each slot must complete its grammatical frame:
//   bio    → "IDENTIFIED AS ___"   (noun phrase: a/an + descriptor)
//   prof   → "WORKING AS ___"      (noun phrase: a/an + role)
//   geo    → "LOCATED IN ___"      (noun phrase: a/an + place type)
//   socio  → "NETWORKED WITHIN ___"(noun phrase: plural + community type)
//   econ   → "VALUED AS ___"       (noun phrase: a/an + economic subject)
//   psycho → "EXHIBITING ___"      (noun phrase: adjective + behaviour/noun)

const VOCABLE_MAP = {

  bio: {
    // completes: "IDENTIFIED AS ___"
    bySignal: {
      gen_z:      ['A YOUNG ADULT',             'AN EARLY-STAGE SUBJECT',    'A DIGITAL NATIVE'],
      millennial: ['A LATE-MILLENNIAL SUBJECT',  'AN ESTABLISHED ADULT',      'A PRIME-AGE INDIVIDUAL'],
      parent:     ['A PARENTAL UNIT',            'A FAMILY-STAGE SUBJECT',    'A CAREGIVER PROFILE'],
      mature:     ['A MATURE INDIVIDUAL',        'A SENIOR-STAGE SUBJECT',    'AN OLDER ADULT'],
      fitness:    ['A BODY-TRACKED SUBJECT',     'A FITNESS-MONITORED ADULT', 'A HIGH-PERFORMANCE BODY'],
      health:     ['A HEALTH-SEEKING SUBJECT',   'A MEDICALLY ACTIVE USER',   'A CLINICAL PROFILE'],
      wellness:   ['A WELLNESS-AWARE SUBJECT',   'A STRESS-MONITORED ADULT',  'AN OPTIMISED SELF'],
    },
    fallback:   ['A BIOLOGICAL SUBJECT',         'A BODY-TRACKED UNIT',       'A BIOLOGICAL PROFILE'],
    poisoned:   ['AN UNDEFINED BODY',            'A BIOLOGICAL NOISE SOURCE', 'AN UNRESOLVABLE SUBJECT', 'A SIGNAL VOID'],
    amplified:  ['A VERIFIED ADULT',             'A HEALTH-OPTIMISED SUBJECT','A PRIME BIOLOGICAL UNIT', 'A CONFIRMED BODY'],
  },

  geo: {
    // completes: "LOCATED IN ___"
    bySignal: {
      urban:      ['AN URBAN ZONE',              'A METROPOLITAN AREA',       'A HIGH-DENSITY LOCALE'],
      traveller:  ['A TRANSIT STATE',            'A MOBILITY CORRIDOR',       'A MULTI-LOCATION ZONE'],
      local:      ['A PLACE-ANCHORED ZONE',      'A TRACKED LOCALITY',        'A LOCAL AREA'],
      commuter:   ['A COMMUTER CORRIDOR',        'A DAILY TRANSIT ZONE',      'A MOBILITY-DENSE AREA'],
      weather:    ['A MONITORED LOCALE',         'A GEO-ACTIVE ZONE',         'A COORDINATE-ANCHORED AREA'],
    },
    fallback:   ['A GEOLOCATED ZONE',            'AN ACTIVE LOCATION',        'A GEOGRAPHIC PROFILE'],
    poisoned:   ['AN UNKNOWN ZONE',              'A LOCATION-NOISE AREA',     'A SPATIAL VOID',          'A COORDINATE-LOST ZONE'],
    amplified:  ['A VERIFIED LOCALE',            'A CONFIRMED GEOGRAPHIC ZONE','A LOCATION-OPTIMISED AREA','A GEO-CONFIRMED ZONE'],
  },

  prof: {
    // completes: "WORKING AS ___"
    bySignal: {
      creative:   ['A CREATIVE PROFESSIONAL',    'A VISUAL PRACTITIONER',     'A DESIGN WORKER'],
      developer:  ['A TECHNICAL WORKER',         'A CODE PRACTITIONER',       'A SOFTWARE DEVELOPER'],
      manager:    ['A MANAGERIAL WORKER',        'AN OPERATIONAL LEAD',       'A MANAGEMENT PROFILE'],
      academic:   ['A KNOWLEDGE WORKER',         'A RESEARCH PRACTITIONER',   'AN ACADEMIC PROFILE'],
      jobseeker:  ['A LABOUR MARKET ACTOR',      'A CAREER-SEEKING SUBJECT',  'A JOB-MARKET PROFILE'],
    },
    fallback:   ['A PROFESSIONAL AGENT',         'AN OCCUPATIONALLY ACTIVE SUBJECT','AN OCCUPATIONAL PROFILE'],
    poisoned:   ['AN UNCLASSIFIED WORKER',       'A ROLE-UNDEFINED SUBJECT',  'A SECTOR-NOISE ENTITY',   'AN OCCUPATION VOID'],
    amplified:  ['A VERIFIED EXPERT',            'A CONFIRMED PROFESSIONAL',  'AN OPTIMISED WORKER',     'A ROLE-CONFIRMED AGENT'],
  },

  econ: {
    // completes: "VALUED AS ___"
    bySignal: {
      shopper:    ['A HIGH-FREQUENCY BUYER',     'AN ACTIVE CONSUMER',        'A PURCHASE-DRIVEN SUBJECT'],
      banking:    ['A FINANCIALLY ACTIVE SUBJECT','A BANKING-TRACKED UNIT',   'A FINANCIAL PROFILE'],
      investor:   ['A RISK-TAKING INVESTOR',     'A MARKET-ACTIVE AGENT',     'A CAPITAL-ENGAGED SUBJECT'],
      insured:    ['A CREDIT-AWARE SUBJECT',     'A DEBT-MANAGED UNIT',       'A FINANCIALLY MANAGED PROFILE'],
    },
    fallback:   ['AN ECONOMICALLY ACTIVE SUBJECT','A SPEND-TRACKED UNIT',     'AN ECONOMIC PROFILE'],
    poisoned:   ['AN ECONOMICALLY UNDEFINED UNIT','A SPEND-NOISE ENTITY',     'AN ECONOMIC VOID',        'A PURCHASE-NULL SUBJECT'],
    amplified:  ['A HIGH-VALUE CONSUMER',        'AN ECONOMICALLY OPTIMISED SUBJECT','A VERIFIED SPENDER','A CONFIRMED ECONOMIC ASSET'],
  },

  socio: {
    // completes: "NETWORKED WITHIN ___"
    bySignal: {
      news:       ['INFORMATION NETWORKS',       'NEWS-TRACKED COMMUNITIES',  'POLITICALLY AWARE CIRCLES'],
      social:     ['PLATFORM-NATIVE NETWORKS',   'SOCIAL MEDIA COMMUNITIES',  'NETWORKED SOCIAL GRAPHS'],
      cultural:   ['CULTURAL COMMUNITIES',       'CULTURALLY ACTIVE CIRCLES', 'CREATIVE SOCIAL NETWORKS'],
      civic:      ['CIVIC NETWORKS',             'POLITICALLY ACTIVE SPACES', 'CIVIC ENGAGEMENT CIRCLES'],
      sports:     ['FAN COMMUNITIES',            'SPORTS-TRACKED NETWORKS',   'AFFINITY GROUPS'],
    },
    fallback:   ['SOCIAL NETWORKS',              'NETWORK-ACTIVE SPACES',     'SOCIO-CULTURAL CIRCLES'],
    poisoned:   ['UNDEFINED AFFILIATIONS',       'FRAGMENTED NETWORKS',       'DISSOLVED SOCIAL CIRCLES','PROFILE-DIFFUSED GROUPS'],
    amplified:  ['VERIFIED CIVIC NETWORKS',      'OPTIMISED SOCIAL CIRCLES',  'CONFIRMED COMMUNITIES',   'NETWORK-CONFIRMED SPACES'],
  },

  psycho: {
    // completes: "EXHIBITING ___"
    bySignal: {
      entertained: ['LEISURE-SEEKING BEHAVIOUR',      'ENTERTAINMENT-DRIVEN PATTERNS', 'PASSIVE CONTENT CONSUMPTION'],
      gamer:       ['PLAY-ORIENTED BEHAVIOUR',        'GAME-ACTIVE PATTERNS',          'LUDIC ENGAGEMENT'],
      organised:   ['ORGANISED COGNITIVE PATTERNS',   'PRODUCTIVITY-TRACKED BEHAVIOUR','EFFICIENCY-ORIENTED HABITS'],
      reader:      ['INFORMATION-SEEKING BEHAVIOUR',  'READING-ACTIVE PATTERNS',       'KNOWLEDGE-DRIVEN TENDENCIES'],
      ai_user:     ['ALGORITHM-ASSISTED BEHAVIOUR',   'MACHINE-MEDIATED PATTERNS',     'AI-DEPENDENT TENDENCIES'],
      learner:     ['GROWTH-ORIENTED BEHAVIOUR',      'LEARNING-ACTIVE PATTERNS',      'KNOWLEDGE-SEEKING TENDENCIES'],
    },
    fallback:   ['TRACKED BEHAVIOURAL PATTERNS',      'BEHAVIOUR-MONITORED TENDENCIES','PSYCHO-BEHAVIOURAL TRAITS'],
    poisoned:   ['UNDEFINED BEHAVIOURAL PATTERNS',    'NOISE-LEVEL BEHAVIOUR',         'DISSOLVED TRAIT PATTERNS', 'VOID-STATE BEHAVIOUR'],
    amplified:  ['OPTIMISED BEHAVIOURAL PATTERNS',    'VERIFIED COGNITIVE TRAITS',     'CONFIRMED BEHAVIOUR PROFILE','REINFORCED PATTERNS'],
  },

};

// ─── Default profile ───────────────────────────────────
// All values start at zero — nothing is assumed about the user.
// Vocables only emerge after real browsing data is collected.
function defaultProfile() {
  const now = Date.now();
  const emptyCategory = () => ({
    score: 0,
    poisonLevel: 0,
    amplifyLevel: 0,
    dataPoints: [],
    isDecontextualized: false,
    weight: 0,
    age: 0,
    propagation: 0,
    topSignal: null   // dominant sub-signal (e.g. 'fitness', 'developer')
  });
  return {
    categories: {
      bio:    emptyCategory(),
      geo:    emptyCategory(),
      prof:   emptyCategory(),
      econ:   emptyCategory(),
      socio:  emptyCategory(),
      psycho: emptyCategory()
    },
    actions: [],
    snapshots: [],
    newDataFlag: false,
    decontextFlags: {},
    lastUpdated: now,
    startedAt: now,
    username: '',
    prefs: {
      decontextAlerts: true,
      dailySnapshot: { enabled: true, hour: 18, minute: 0 }
    }
  };
}

// ─── Compute vocable from category state ──────────────
// Reads state.topSignal (set by classifier) to pick the
// appropriate label tier. Works for ANY user's browsing.
function computeVocable(cat, state) {
  const map = VOCABLE_MAP[cat];
  if (!map) return null;

  // No data yet
  if (!state || (state.score === 0 && state.poisonLevel === 0 && state.amplifyLevel === 0)) {
    return null;
  }

  // Poison override
  if (state.poisonLevel > 0) {
    const idx = Math.min(state.poisonLevel, map.poisoned.length - 1);
    return map.poisoned[idx];
  }

  // Amplify override
  if (state.amplifyLevel > 0) {
    const idx = Math.min(state.amplifyLevel, map.amplified.length - 1);
    return map.amplified[idx];
  }

  // Geo: prefer the concrete locality detected from search queries
  // over the generic signal-based label.
  if (cat === 'geo' && state.detectedLocality) {
    return state.detectedLocality;
  }

  // Pick tier based on score: 1-10 → tier0, 11-30 → tier1, 31+ → tier2
  const tier = state.score < 11 ? 0 : state.score < 31 ? 1 : 2;

  // Use topSignal if available
  const sig = state.topSignal;
  if (sig && map.bySignal[sig]) {
    return map.bySignal[sig][Math.min(tier, map.bySignal[sig].length - 1)];
  }

  // Fallback
  return map.fallback[Math.min(tier, map.fallback.length - 1)];
}

// ─── Locality extraction ──────────────────────────────
// Build a flat dictionary of known cities/regions/countries.
// Match is longest-first (so "new york" wins over "new"),
// case-insensitive, on word boundaries.
const LOCALITIES = [
  // Italy
  { match: 'milano',    label: 'MILAN' },
  { match: 'milan',     label: 'MILAN' },
  { match: 'roma',      label: 'ROME' },
  { match: 'rome',      label: 'ROME' },
  { match: 'firenze',   label: 'FLORENCE' },
  { match: 'florence',  label: 'FLORENCE' },
  { match: 'venezia',   label: 'VENICE' },
  { match: 'venice',    label: 'VENICE' },
  { match: 'napoli',    label: 'NAPLES' },
  { match: 'naples',    label: 'NAPLES' },
  { match: 'torino',    label: 'TURIN' },
  { match: 'turin',     label: 'TURIN' },
  { match: 'bologna',   label: 'BOLOGNA' },
  { match: 'genova',    label: 'GENOA' },
  { match: 'genoa',     label: 'GENOA' },
  { match: 'palermo',   label: 'PALERMO' },
  { match: 'verona',    label: 'VERONA' },
  { match: 'trieste',   label: 'TRIESTE' },
  { match: 'bari',      label: 'BARI' },
  { match: 'catania',   label: 'CATANIA' },
  // Europe
  { match: 'london',     label: 'LONDON' },
  { match: 'paris',      label: 'PARIS' },
  { match: 'berlin',     label: 'BERLIN' },
  { match: 'madrid',     label: 'MADRID' },
  { match: 'barcelona',  label: 'BARCELONA' },
  { match: 'amsterdam',  label: 'AMSTERDAM' },
  { match: 'lisboa',     label: 'LISBON' },
  { match: 'lisbon',     label: 'LISBON' },
  { match: 'vienna',     label: 'VIENNA' },
  { match: 'wien',       label: 'VIENNA' },
  { match: 'zurich',     label: 'ZURICH' },
  { match: 'zürich',     label: 'ZURICH' },
  { match: 'geneva',     label: 'GENEVA' },
  { match: 'brussels',   label: 'BRUSSELS' },
  { match: 'munich',     label: 'MUNICH' },
  { match: 'münchen',    label: 'MUNICH' },
  { match: 'frankfurt',  label: 'FRANKFURT' },
  { match: 'hamburg',    label: 'HAMBURG' },
  { match: 'copenhagen', label: 'COPENHAGEN' },
  { match: 'stockholm',  label: 'STOCKHOLM' },
  { match: 'oslo',       label: 'OSLO' },
  { match: 'helsinki',   label: 'HELSINKI' },
  { match: 'dublin',     label: 'DUBLIN' },
  { match: 'edinburgh',  label: 'EDINBURGH' },
  { match: 'athens',     label: 'ATHENS' },
  { match: 'budapest',   label: 'BUDAPEST' },
  { match: 'prague',     label: 'PRAGUE' },
  { match: 'warsaw',     label: 'WARSAW' },
  { match: 'istanbul',   label: 'ISTANBUL' },
  // North America
  { match: 'new york',     label: 'NEW YORK' },
  { match: 'manhattan',    label: 'NEW YORK' },
  { match: 'brooklyn',     label: 'NEW YORK' },
  { match: 'los angeles',  label: 'LOS ANGELES' },
  { match: 'san francisco',label: 'SAN FRANCISCO' },
  { match: 'chicago',      label: 'CHICAGO' },
  { match: 'boston',       label: 'BOSTON' },
  { match: 'seattle',      label: 'SEATTLE' },
  { match: 'austin',       label: 'AUSTIN' },
  { match: 'miami',        label: 'MIAMI' },
  { match: 'washington',   label: 'WASHINGTON' },
  { match: 'toronto',      label: 'TORONTO' },
  { match: 'montreal',     label: 'MONTRÉAL' },
  { match: 'vancouver',    label: 'VANCOUVER' },
  { match: 'mexico city',  label: 'MEXICO CITY' },
  // Asia / Pacific
  { match: 'tokyo',       label: 'TOKYO' },
  { match: 'kyoto',       label: 'KYOTO' },
  { match: 'osaka',       label: 'OSAKA' },
  { match: 'seoul',       label: 'SEOUL' },
  { match: 'beijing',     label: 'BEIJING' },
  { match: 'shanghai',    label: 'SHANGHAI' },
  { match: 'hong kong',   label: 'HONG KONG' },
  { match: 'singapore',   label: 'SINGAPORE' },
  { match: 'bangkok',     label: 'BANGKOK' },
  { match: 'mumbai',      label: 'MUMBAI' },
  { match: 'delhi',       label: 'DELHI' },
  { match: 'dubai',       label: 'DUBAI' },
  { match: 'sydney',      label: 'SYDNEY' },
  { match: 'melbourne',   label: 'MELBOURNE' },
  // Latin America / Africa
  { match: 'são paulo',   label: 'SÃO PAULO' },
  { match: 'sao paulo',   label: 'SÃO PAULO' },
  { match: 'rio de janeiro', label: 'RIO DE JANEIRO' },
  { match: 'buenos aires',label: 'BUENOS AIRES' },
  { match: 'cape town',   label: 'CAPE TOWN' },
  { match: 'johannesburg',label: 'JOHANNESBURG' },
  { match: 'lagos',       label: 'LAGOS' },
  { match: 'nairobi',     label: 'NAIROBI' },
  { match: 'cairo',       label: 'CAIRO' },
  // Countries (fallback when no city found)
  { match: 'italia',         label: 'ITALY' },
  { match: 'italy',          label: 'ITALY' },
  { match: 'france',         label: 'FRANCE' },
  { match: 'francia',        label: 'FRANCE' },
  { match: 'germany',        label: 'GERMANY' },
  { match: 'germania',       label: 'GERMANY' },
  { match: 'deutschland',    label: 'GERMANY' },
  { match: 'spain',          label: 'SPAIN' },
  { match: 'españa',         label: 'SPAIN' },
  { match: 'spagna',         label: 'SPAIN' },
  { match: 'portugal',       label: 'PORTUGAL' },
  { match: 'portogallo',     label: 'PORTUGAL' },
  { match: 'switzerland',    label: 'SWITZERLAND' },
  { match: 'svizzera',       label: 'SWITZERLAND' },
  { match: 'united kingdom', label: 'UNITED KINGDOM' },
  { match: 'great britain',  label: 'UNITED KINGDOM' },
  { match: 'england',        label: 'UNITED KINGDOM' },
  { match: 'united states',  label: 'UNITED STATES' },
  { match: 'usa',            label: 'UNITED STATES' },
  { match: 'canada',         label: 'CANADA' },
  { match: 'mexico',         label: 'MEXICO' },
  { match: 'brazil',         label: 'BRAZIL' },
  { match: 'brasil',         label: 'BRAZIL' },
  { match: 'argentina',      label: 'ARGENTINA' },
  { match: 'japan',          label: 'JAPAN' },
  { match: 'china',          label: 'CHINA' },
  { match: 'india',          label: 'INDIA' },
  { match: 'australia',      label: 'AUSTRALIA' },
  { match: 'south africa',   label: 'SOUTH AFRICA' },
  { match: 'egypt',          label: 'EGYPT' },
];

// Pre-sort once by length DESC so "new york" beats "york".
const LOCALITIES_BY_LENGTH = LOCALITIES.slice().sort((a, b) => b.match.length - a.match.length);

// Extract a search query string from a URL (Google, Bing, DDG, Ecosia,
// Qwant, Yahoo, Baidu, Yandex). Returns "" if not a search URL.
function extractSearchQuery(url) {
  try {
    const u = new URL(url);
    const host = u.hostname.replace(/^www\./, '');
    const isSearch =
      host.includes('google.')        ||
      host.includes('bing.com')       ||
      host.includes('duckduckgo.com') ||
      host.includes('ecosia.org')     ||
      host.includes('qwant.com')      ||
      host.includes('search.yahoo.')  ||
      host.includes('baidu.com')      ||
      host.includes('yandex.');
    if (!isSearch) return '';
    // Common query params: q (most), p (Yahoo legacy), wd (Baidu), text (Yandex)
    const q = u.searchParams.get('q') || u.searchParams.get('p') ||
              u.searchParams.get('wd') || u.searchParams.get('text') || '';
    return decodeURIComponent(q.replace(/\+/g, ' '));
  } catch {
    return '';
  }
}

// Find the most-specific locality mentioned in a search query or page title.
// Returns the canonical label (e.g., "MILAN") or null.
function findLocality(searchQuery, title) {
  const haystack = ((searchQuery || '') + ' ' + (title || '')).toLowerCase();
  if (!haystack.trim()) return null;
  for (const loc of LOCALITIES_BY_LENGTH) {
    // word-boundary check so "ash" doesn't match "washington" — but we
    // do allow punctuation/spaces around the match
    const re = new RegExp(`(^|[^a-z0-9])${loc.match.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}([^a-z0-9]|$)`, 'i');
    if (re.test(haystack)) return loc.label;
  }
  return null;
}

// ─── Classify URL + detect dominant sub-signal ────────
// Match a keyword against the text. Short alphabetic keywords (≤5 chars)
// must be bounded by non-alphanumerics on both sides to avoid silly
// substring false-positives (e.g. "ign" inside "design", "cv" inside "Mvcvz",
// "tax" inside "syntax"). Multi-word keywords ("near me") and long keywords
// keep simple substring matching to allow partial domain matches like
// "google.com/maps".
function matchKeyword(text, kw) {
  if (kw.length > 5 || !/^[a-z][a-z']*$/i.test(kw)) {
    return text.includes(kw);
  }
  const esc = kw.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  const re = new RegExp(`(^|[^a-z0-9])${esc}([^a-z0-9]|$)`, 'i');
  return re.test(text);
}

function classifyUrl(url, title) {
  const text = (url + ' ' + (title || '')).toLowerCase();

  let bestCat = null;
  let bestScore = 0;
  let bestSignal = null;
  let secondCat = null;
  let secondScore = 0;

  for (const [cat, def] of Object.entries(CLASSIFIERS)) {
    // Count keyword hits
    const hits = def.keywords.filter(kw => matchKeyword(text, kw)).length;
    if (hits === 0) continue;

    // Find dominant sub-signal
    let topSig = null;
    let topSigCount = 0;
    for (const [sig, sigKws] of Object.entries(def.signals)) {
      const count = sigKws.filter(kw => matchKeyword(text, kw)).length;
      if (count > topSigCount) { topSigCount = count; topSig = sig; }
    }

    if (hits > bestScore) {
      secondCat = bestCat; secondScore = bestScore;
      bestCat = cat; bestScore = hits; bestSignal = topSig;
    } else if (hits > secondScore) {
      secondCat = cat; secondScore = hits;
    }
  }

  if (!bestCat) return null;

  return {
    primary: bestCat,
    secondary: secondCat,
    score: bestScore,
    topSignal: bestSignal,
    url,
    title,
    timestamp: Date.now()
  };
}

// ─── Decontextualization model ────────────────────────
// Only flags DANGEROUS cross-context uses: when data collected
// in one context is exploited for inferences in a different,
// unexpected domain that could harm the user.
//
// NOT flagged: legitimate overlap (LinkedIn → age + profession,
// location → commute + weather, social + psycho patterns).
//
// FLAGGED: data migrating toward gatekeeping domains
// (insurance, credit, employment, political profiling).

const DECONTEXT_RULES = [
  // Health/body data → financial profiling (insurance, credit scoring)
  {
    from: 'bio', to: 'econ',
    reason: 'Health data detected in financial context. Medical searches may be used to assess insurance risk or creditworthiness.'
  },
  // Health/body data → professional screening
  {
    from: 'bio', to: 'prof',
    reason: 'Health data detected in professional context. Medical history may be used to filter job candidates or assess workplace risk.'
  },
  // Location → political/social profiling
  {
    from: 'geo', to: 'socio',
    reason: 'Location data detected in socio-political context. Where you live or travel may be used to infer political orientation or social affiliation.'
  },
  // Psychological/behavioural patterns → employment screening
  {
    from: 'psycho', to: 'prof',
    reason: 'Behavioural patterns detected in professional context. App usage, entertainment habits and productivity tools may be used to profile cognitive traits for employment.'
  },
  // Psychological/behavioural patterns → credit or financial scoring
  {
    from: 'psycho', to: 'econ',
    reason: 'Behavioural data detected in financial context. Digital habits and entertainment patterns may be used to infer financial reliability or spending risk.'
  },
  // Political/civic activity → economic discrimination
  {
    from: 'socio', to: 'econ',
    reason: 'Socio-political activity detected in financial context. Community engagement or political views may influence pricing, credit access or commercial targeting.'
  },
  // Location → economic discrimination (pricing, offers)
  {
    from: 'geo', to: 'econ',
    reason: 'Location data detected in financial context. Geographic profiling may be used for dynamic pricing, loan refusals or targeted commercial discrimination.'
  },
  // Demographic signals → social/political profiling
  {
    from: 'bio', to: 'socio',
    reason: 'Bio-demographic data detected in socio-political context. Age, health or life-stage signals may be used to target political messaging or manipulate voting behaviour.'
  },
];

function detectDecontextualization(primary, secondary) {
  if (!primary || !secondary) return null;
  const rule = DECONTEXT_RULES.find(r =>
    (r.from === primary && r.to === secondary) ||
    (r.from === secondary && r.to === primary)
  );
  return rule || null;
}

// ─── Update profile from navigation ───────────────────
async function updateProfile(url, title) {
  // Skip extension pages, new tab, etc.
  if (!url || url.startsWith('chrome') || url.startsWith('about') || url.startsWith('moz-extension')) return;

  const result = await chrome.storage.local.get('profile');
  const profile = result.profile || defaultProfile();

  const classification = classifyUrl(url, title);
  if (!classification) return;

  const { primary, secondary, score, decontext, timestamp } = classification;

  // Update primary category
  const cat = profile.categories[primary];
  cat.score = Math.min(100, cat.score + score * 2);
  cat.weight = Math.min(1.0, cat.weight + 0.02);
  cat.age = Math.min(10, cat.age + 0.1);

  // Detect per-point decontextualization (only when this specific URL
  // is classified into primary BUT also matches a dangerous secondary).
  let pointDecontextualized = false;
  if (secondary) {
    const rule = detectDecontextualization(primary, secondary);
    if (rule) {
      pointDecontextualized = true;
      cat.isDecontextualized = true;
      profile.decontextFlags[primary] = {
        detectedAt: timestamp,
        reason: rule.reason,
        from: rule.from,
        to: rule.to,
        url
      };
    }
  }

  const newPoint = { url, title, timestamp, signal: classification.topSignal||null };
  if (pointDecontextualized) newPoint.decontextualized = true;

  // Locality detection from search queries / page title.
  // Track for the geo category regardless of which category is primary —
  // a search for "milan restaurant" can primary-classify as geo OR econ,
  // but the locality is still useful.
  const searchQuery = extractSearchQuery(url);
  const locality = findLocality(searchQuery, title);
  if (locality) {
    newPoint.locality = locality;
    const geo = profile.categories.geo;
    geo.detectedLocality = locality;
    geo.localityDetectedAt = timestamp;
    // Make sure geo has SOME signal so computeVocable returns the locality
    // (without this, score may stay at 0 for the searcher case).
    geo.score = Math.min(100, (geo.score || 0) + 1);
  }
  cat.dataPoints = [newPoint, ...cat.dataPoints].slice(0, 20);

  // Update topSignal: keep the most recently seen signal
  if (classification.topSignal) {
    cat.topSignal = classification.topSignal;
  }

  profile.newDataFlag = true;
  profile.lastUpdated = timestamp;

  // Propagation: spread slightly to adjacent categories
  if (secondary && profile.categories[secondary]) {
    profile.categories[secondary].score = Math.min(100, profile.categories[secondary].score + 1);
    profile.categories[secondary].propagation = Math.min(10, profile.categories[secondary].propagation + 0.2);
  }

  await chrome.storage.local.set({ profile });
}

// ─── Listen to tab navigation ──────────────────────────
chrome.tabs.onUpdated.addListener((tabId, changeInfo, tab) => {
  if (changeInfo.status === 'complete' && tab.url) {
    updateProfile(tab.url, tab.title);
  }
});

// ─────────────────────────────────────────────────────────────
// ARDUINO BRIDGE
// Comunicazione bidirezionale con il Nano ESP32 via HTTP locale
// ─────────────────────────────────────────────────────────────

let arduinoIp        = null;   // IP del device, impostato dal popup
let arduinoConnected = false;
let arduinoPollTimer = null;
let lastSentenceSent = '';     // evita POST ridondanti se la frase non cambia

// Carica IP salvato all'avvio
chrome.storage.local.get('arduinoIp').then(r => {
  if (r.arduinoIp) {
    arduinoIp = r.arduinoIp;
    startArduinoBridge();
  }
});

// Ping device e verifica connessione
async function arduinoPing(ip) {
  console.log('[Replica/SW] arduinoPing →', ip);
  const url = `http://${ip}/status`;
  try {
    const r = await fetch(url, {
      method: 'GET',
      mode: 'cors',
      cache: 'no-store',
      signal: AbortSignal.timeout(8000),
    });
    console.log('[Replica/SW] /status status:', r.status, r.ok);
    if (!r.ok) return false;
    const d = await r.json();
    console.log('[Replica/SW] /status body:', d);
    return d.connected === true;
  } catch (e) {
    console.error('[Replica/SW] arduinoPing fetch failed:', e?.name, e?.message, e);
    return false;
  }
}

// Invia la frase corrente al display Arduino, includendo la mappa
// categoria→vocabolo (per evidenziare la parola selezionata
// sull'encoder) e l'elenco delle parole decontestualizzate (per il
// blink del LED).
async function sendSentenceToArduino(text) {
  if (!arduinoIp || !arduinoConnected) return;

  let decontext = [];
  const vocables = {};
  try {
    const pr = await chrome.storage.local.get('profile');
    const profile = pr.profile;
    if (profile) {
      const sentence = buildSentence(profile);
      for (const c of CATEGORIES) {
        const v = sentence.vocables[c];
        if (v) vocables[c] = String(v).toUpperCase();
      }
      if (profile.decontextFlags) {
        const dcCats = Object.keys(profile.decontextFlags);
        if (dcCats.length) {
          decontext = dcCats
            .map(c => sentence.vocables[c])
            .filter(v => !!v)
            .map(v => String(v).toUpperCase());
        }
      }
    }
  } catch { /* fallback con array vuoto */ }

  const cacheKey = text + '|' + decontext.join(',') + '|' + JSON.stringify(vocables);
  if (cacheKey === lastSentenceSent) return;
  try {
    await fetch(`http://${arduinoIp}/sentence`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ text, decontext, vocables }),
      signal: AbortSignal.timeout(3000)
    });
    lastSentenceSent = cacheKey;
  } catch { /* device offline, riprova al prossimo ciclo */ }
}

// Applica un'azione (poison/amplify) al profilo
// Simula la stessa logica del toggle nel popup
async function applyArduinoAction(action, targetCat, intensity) {
  console.log('[Replica/SW] applyArduinoAction action=', action, 'targetCat=', targetCat, 'intensity=', intensity);
  const r = await chrome.storage.local.get('profile');
  const profile = r.profile || defaultProfile();
  const sentence = buildSentence(profile);
  const timestamp = Date.now();

  // Applica l'azione solo alla categoria target (selezionata via
  // encoder 1 sul device). Se manca o non valida, fallback a tutte
  // le categorie con un vocabolo attivo per retrocompatibilità.
  const targets = (targetCat && profile.categories[targetCat])
    ? [targetCat]
    : CATEGORIES.filter(c => !!sentence.vocables[c]);

  // Intensità 1..3 dal device. Se 0/null usa la legacy "+1".
  const explicitLevel = (intensity >= 1 && intensity <= 3) ? intensity : null;

  for (const cat of targets) {
    const st = profile.categories[cat];
    const voc = sentence.vocables[cat];
    if (!voc) continue;

    const prevVoc = voc;
    // Per l'eventuale undo: memorizza lo stato prima del cambio.
    const prevPoison = st.poisonLevel;
    const prevAmplify = st.amplifyLevel;

    if (action === 'poison') {
      st.poisonLevel = explicitLevel != null ? explicitLevel : Math.min(3, st.poisonLevel + 1);
      st.amplifyLevel = 0;
    } else if (action === 'amplify') {
      st.amplifyLevel = explicitLevel != null ? explicitLevel : Math.min(3, st.amplifyLevel + 1);
      st.poisonLevel = 0;
    }

    // Poison/amplify "cura" la decontestualizzazione: la categoria
    // ha ricevuto un nuovo segnale dall'utente, l'alert/badge sparisce.
    // Le voci storiche (dataPoints.decontextualized e gli eventi
    // "DECONTEXTUALISED" in profile.actions) restano per la timeline.
    if (action === 'poison' || action === 'amplify') {
      const wasDC = !!st.isDecontextualized;
      st.isDecontextualized = false;
      if (profile.decontextFlags && profile.decontextFlags[cat]) {
        delete profile.decontextFlags[cat];
      }
      console.log('[Replica/SW]   cleared decontext on', cat, 'wasDC=', wasDC);
    }

    const newVoc = computeVocable(cat, st);
    if (newVoc !== prevVoc) {
      profile.actions = [{
        category:    cat,
        action,
        fromVocable: prevVoc,
        toVocable:   newVoc,
        timestamp,
        intensity:   action === 'poison' ? st.poisonLevel : st.amplifyLevel,
        prevPoison,           // per undo
        prevAmplify
      }, ...(profile.actions||[])].slice(0, 100);
    }
  }

  profile.lastUpdated = timestamp;
  await chrome.storage.local.set({ profile });
  return buildSentence(profile);
}

// Annulla l'ultima azione (poison/amplify) sulla categoria target.
// Cerca la voce più recente in profile.actions con cat e action
// rilevanti, ripristina i livelli precedenti e rimuove la voce
// dalla cronologia (così "Signals collected" non mostra una azione
// che è stata revertita).
async function undoArduinoAction(targetCat) {
  console.log('[Replica/SW] undoArduinoAction targetCat=', targetCat);
  const r = await chrome.storage.local.get('profile');
  const profile = r.profile || defaultProfile();
  if (!targetCat || !profile.categories[targetCat]) {
    return buildSentence(profile);
  }
  const acts = profile.actions || [];
  const idx = acts.findIndex(a => a.category === targetCat && (a.action === 'poison' || a.action === 'amplify'));
  if (idx < 0) {
    console.log('[Replica/SW]   no action to undo for', targetCat);
    return buildSentence(profile);
  }
  const a = acts[idx];
  const st = profile.categories[targetCat];
  if (typeof a.prevPoison === 'number')  st.poisonLevel  = a.prevPoison;
  if (typeof a.prevAmplify === 'number') st.amplifyLevel = a.prevAmplify;
  acts.splice(idx, 1);
  profile.actions = acts;
  profile.lastUpdated = Date.now();
  await chrome.storage.local.set({ profile });
  return buildSentence(profile);
}

// Reset livelli poison/amplify (quando switch torna in OBSERVE)
async function resetArduinoLevels() {
  const r = await chrome.storage.local.get('profile');
  const profile = r.profile || defaultProfile();
  for (const cat of CATEGORIES) {
    profile.categories[cat].poisonLevel  = 0;
    profile.categories[cat].amplifyLevel = 0;
  }
  await chrome.storage.local.set({ profile });
}

// Polling switch Arduino: legge /action ogni 2s
async function pollArduinoAction() {
  if (!arduinoIp) return;
  try {
    const r = await fetch(`http://${arduinoIp}/action`, { signal: AbortSignal.timeout(2000) });
    if (!r.ok) { arduinoConnected = false; return; }
    const d = await r.json();
    arduinoConnected = true;

    // d.action: 'poison' | 'amplify' | 'undo' | 'neutral' (legacy 'observe').
    // d.fresh==true => applica adesso. d.intensity 1..3 per poison/amplify.
    const noopMode = (d.action === 'neutral' || d.action === 'observe');

    if (d.fresh && (d.action === 'poison' || d.action === 'amplify')) {
      const sentence = await applyArduinoAction(d.action, d.category, d.intensity|0);
      await sendSentenceToArduino(sentence.text);
    } else if (d.fresh && d.action === 'undo') {
      const sentence = await undoArduinoAction(d.category);
      await sendSentenceToArduino(sentence.text);
    } else if (noopMode) {
      // Modo neutro: nessuna modifica al profilo, solo sync della frase.
      const pr = await chrome.storage.local.get('profile');
      const s = buildSentence(pr.profile || defaultProfile());
      await sendSentenceToArduino(s.text);
    }
  } catch {
    arduinoConnected = false;
  }
}

function startArduinoBridge() {
  if (arduinoPollTimer) clearInterval(arduinoPollTimer);
  arduinoPollTimer = setInterval(pollArduinoAction, 2000);
  // Ping immediato + invia frase subito
  arduinoPing(arduinoIp).then(ok => {
    arduinoConnected = ok;
    if (ok) {
      chrome.storage.local.get('profile').then(r => {
        const s = buildSentence(r.profile || defaultProfile());
        sendSentenceToArduino(s.text);
      });
    }
  });
}

function stopArduinoBridge() {
  if (arduinoPollTimer) { clearInterval(arduinoPollTimer); arduinoPollTimer = null; }
  arduinoConnected = false;
  lastSentenceSent = '';
}

// ─── Shared message handler logic ─────────────────────
function handleMessage(msg, sender, sendResponse) {
  if (msg.type === 'GET_PROFILE') {
    chrome.storage.local.get('profile').then(r => {
      const profile = r.profile || defaultProfile();
      const sentence = buildSentence(profile);
      sendResponse({ profile, sentence, arduino: { connected: arduinoConnected, ip: arduinoIp } });
    });
    return true;
  }

  if (msg.type === 'RESET') {
    chrome.storage.local.set({ profile: defaultProfile() }).then(() => sendResponse({ ok: true }));
    return true;
  }

  if (msg.type === 'CLEAR_FLAG') {
    chrome.storage.local.get('profile').then(r => {
      const profile = r.profile || defaultProfile();
      profile.newDataFlag = false;
      chrome.storage.local.set({ profile }).then(() => sendResponse({ ok: true }));
    });
    return true;
  }

  if (msg.type === 'SAVE_SNAPSHOT') {
    chrome.storage.local.get('profile').then(r => {
      const profile = r.profile || defaultProfile();
      const sentence = buildSentence(profile);
      const now = new Date();
      const pad = n => String(n).padStart(2, '0');
      const dateStr = `${now.getFullYear()}-${pad(now.getMonth()+1)}-${pad(now.getDate())}`;
      const snap = {
        date: dateStr,                       // YYYY-MM-DD in LOCAL time
        savedAt: now.toISOString(),
        sentence: sentence.text,
        vocables: sentence.vocables,
        manual: true,
        timestamp: Date.now(),
      };
      profile.snapshots = [snap, ...(profile.snapshots || [])].slice(0, 60);
      chrome.storage.local.set({ profile }).then(() => sendResponse({ ok: true, snapshot: snap }));
    });
    return true;
  }

  if (msg.type === 'SET_PREFERENCES') {
    chrome.storage.local.get('profile').then(r => {
      const profile = r.profile || defaultProfile();
      const prefs = profile.prefs || { decontextAlerts: true, dailySnapshot: { enabled: true, hour: 18, minute: 0 } };
      if (typeof msg.decontextAlerts === 'boolean') prefs.decontextAlerts = msg.decontextAlerts;
      if (msg.dailySnapshot && typeof msg.dailySnapshot === 'object') {
        const ds = prefs.dailySnapshot || { enabled: true, hour: 18, minute: 0 };
        if (typeof msg.dailySnapshot.enabled === 'boolean') ds.enabled = msg.dailySnapshot.enabled;
        if (Number.isFinite(msg.dailySnapshot.hour)) ds.hour = Math.max(0, Math.min(23, msg.dailySnapshot.hour|0));
        if (Number.isFinite(msg.dailySnapshot.minute)) ds.minute = Math.max(0, Math.min(59, msg.dailySnapshot.minute|0));
        prefs.dailySnapshot = ds;
      }
      profile.prefs = prefs;
      chrome.storage.local.set({ profile }).then(() => sendResponse({ ok: true, prefs }));
    });
    return true;
  }

  if (msg.type === 'SET_USERNAME') {
    chrome.storage.local.get('profile').then(r => {
      const profile = r.profile || defaultProfile();
      profile.username = String(msg.username || '').slice(0, 60);
      chrome.storage.local.set({ profile }).then(() => sendResponse({ ok: true, username: profile.username }));
    });
    return true;
  }

  if (msg.type === 'RESET_SNAPSHOTS') {
    chrome.storage.local.get('profile').then(r => {
      const profile = r.profile || defaultProfile();
      profile.snapshots = [];
      chrome.storage.local.set({ profile }).then(() => sendResponse({ ok: true }));
    });
    return true;
  }

  if (msg.type === 'DELETE_SNAPSHOT') {
    chrome.storage.local.get('profile').then(r => {
      const profile = r.profile || defaultProfile();
      const ts = msg.timestamp;
      const before = (profile.snapshots || []).length;
      profile.snapshots = (profile.snapshots || []).filter(s => s.timestamp !== ts);
      const removed = before - profile.snapshots.length;
      chrome.storage.local.set({ profile }).then(() => sendResponse({ ok: removed > 0 }));
    });
    return true;
  }

  // ── Arduino messages ────────────────────────────────
  if (msg.type === 'ARDUINO_CONNECT') {
    console.log('[Replica/SW] ARDUINO_CONNECT received with ip =', msg.ip);
    const ip = (msg.ip || '').trim();
    if (!ip) { sendResponse({ ok: false, error: 'No IP provided' }); return true; }
    arduinoIp = ip;
    chrome.storage.local.set({ arduinoIp: ip });
    arduinoPing(ip).then(ok => {
      arduinoConnected = ok;
      if (ok) {
        startArduinoBridge();
        // Invia subito la frase corrente
        chrome.storage.local.get('profile').then(r => {
          const s = buildSentence(r.profile || defaultProfile());
          sendSentenceToArduino(s.text);
        });
      }
      sendResponse({ ok, error: ok ? null : 'Device not reachable' });
    });
    return true;
  }

  if (msg.type === 'ARDUINO_DISCONNECT') {
    stopArduinoBridge();
    arduinoIp = null;
    chrome.storage.local.remove('arduinoIp');
    sendResponse({ ok: true });
    return true;
  }

  if (msg.type === 'ARDUINO_STATUS') {
    sendResponse({ connected: arduinoConnected, ip: arduinoIp });
    return true;
  }
}

// Internal (popup, extension pages)
chrome.runtime.onMessage.addListener(handleMessage);

// External (webapp on localhost via externally_connectable)
chrome.runtime.onMessageExternal.addListener(handleMessage);

// ─── Build algorithmic sentence ───────────────────────
function buildSentence(profile) {
  const v = {};
  for (const cat of CATEGORIES) {
    v[cat] = computeVocable(cat, profile.categories[cat]);
  }
  // Bracket-wrap each slot (like the popup UI) so the e-ink display
  // shows the same shape, e.g. "IDENTIFIED AS [YOUNG], WORKING AS […]".
  const slot = (val) => `[${val || '?'}]`;
  return {
    text: `IDENTIFIED AS ${slot(v.bio)}, WORKING AS ${slot(v.prof)}, LOCATED IN ${slot(v.geo)}, NETWORKED WITHIN ${slot(v.socio)}, VALUED AS ${slot(v.econ)}, AND EXHIBITING ${slot(v.psycho)}.`,
    vocables: v
  };
}

// ─── Daily snapshot at the time configured in profile.prefs ─────────
function checkDailySnapshot() {
  const now = new Date();
  chrome.storage.local.get('profile').then(r => {
    if (!r.profile) return;
    const profile = r.profile;
    const ds = profile.prefs && profile.prefs.dailySnapshot;
    if (!ds || !ds.enabled) return;
    if (now.getHours() !== (ds.hour|0) || now.getMinutes() !== (ds.minute|0)) return;
    const sentence = buildSentence(profile);
    const pad = n => String(n).padStart(2, '0');
    const dateStr = `${now.getFullYear()}-${pad(now.getMonth()+1)}-${pad(now.getDate())}`;
    const snap = {
      date: dateStr,
      sentence: sentence.text,
      vocables: sentence.vocables,
      timestamp: Date.now()
    };
    profile.snapshots = [snap, ...profile.snapshots].slice(0, 30);
    chrome.storage.local.set({ profile });
  });
}

setInterval(checkDailySnapshot, 60000);

// ─── Init ─────────────────────────────────────────────
chrome.storage.local.get('profile').then(r => {
  if (!r.profile) {
    chrome.storage.local.set({ profile: defaultProfile() });
  }
});
