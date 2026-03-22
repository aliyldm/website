// ========================================
// APP STATE
// ========================================
const state = {
    currentPage: 'catalog',
    searchQuery: '',
    rankingSearchQuery: '',
    selectedCategory: null,
    selectedSubcategory: null,
    allParts: [],
    categories: {},
    categoryColors: {},
    favorites: JSON.parse(localStorage.getItem('favorites')) || [],
    theme: localStorage.getItem('theme') || 'light'
};

// Category color map
const CATEGORY_COLOR_MAP = {
    'Filtreler': { color: '#10b981', bg: '#ecfdf5' },
    'Fren Sistemi': { color: '#ef4444', bg: '#fef2f2' },
    'Direksiyon ve Süspansiyon': { color: '#3b82f6', bg: '#eff6ff' },
    'Aks ve Aktarma': { color: '#f59e0b', bg: '#fffbeb' },
    'Motor Mekanik': { color: '#8b5cf6', bg: '#f5f3ff' },
    'Conta ve Keçe': { color: '#ec4899', bg: '#fdf2f8' },
    'Soğutma Sistemi': { color: '#06b6d4', bg: '#ecfeff' },
    'Elektrik ve Elektronik': { color: '#f97316', bg: '#fff7ed' },
    'Egzoz Sistemi': { color: '#6b7280', bg: '#f9fafb' },
    'Yakıt Sistemi': { color: '#84cc16', bg: '#f7fee7' },
    'Klima Sistemi': { color: '#14b8a6', bg: '#f0fdfa' },
    'Şanzıman ve Debriyaj': { color: '#a855f7', bg: '#faf5ff' },
    'Kaporta ve Dış': { color: '#64748b', bg: '#f8fafc' },
    'İç Mekan': { color: '#d946ef', bg: '#fdf4ff' },
    'Aydınlatma': { color: '#eab308', bg: '#fefce8' },
    'Cam ve Ayna': { color: '#0ea5e9', bg: '#f0f9ff' },
    'Lastik ve Jant': { color: '#dc2626', bg: '#fef2f2' },
    'Akü ve Şarj': { color: '#16a34a', bg: '#f0fdf4' },
};

const DEFAULT_COLOR = { color: '#6366f1', bg: '#eef2ff' };

function getCategoryColor(category) {
    return CATEGORY_COLOR_MAP[category] || DEFAULT_COLOR;
}

// ========================================
// INITIALIZATION
// ========================================
document.addEventListener('DOMContentLoaded', () => {
    initData();
    applyTheme();
    renderCategories();
    renderParts();
    renderRanking();
    renderFavorites();
    updateStats();
    setupEventListeners();
});

function initData() {
    state.allParts = VERI.parts || [];
    // Build category -> subcategories map
    state.categories = {};
    state.allParts.forEach(p => {
        if (!state.categories[p.kategori]) {
            state.categories[p.kategori] = new Set();
        }
        if (p.altKategori) {
            state.categories[p.kategori].add(p.altKategori);
        }
    });
}

function setupEventListeners() {
    // Catalog search
    const searchInput = document.getElementById('search-input');
    searchInput.addEventListener('input', debounce((e) => {
        state.searchQuery = e.target.value.trim();
        document.getElementById('search-clear').style.display = state.searchQuery ? 'flex' : 'none';
        renderParts();
    }, 200));

    // Ranking search
    const rankingSearchInput = document.getElementById('ranking-search-input');
    rankingSearchInput.addEventListener('input', debounce((e) => {
        state.rankingSearchQuery = e.target.value.trim();
        document.getElementById('ranking-search-clear').style.display = state.rankingSearchQuery ? 'flex' : 'none';
        renderRanking();
    }, 200));

    // Close modal with Escape
    document.addEventListener('keydown', (e) => {
        if (e.key === 'Escape') closeModal();
    });
}

// ========================================
// PAGE SWITCHING
// ========================================
function switchPage(page) {
    state.currentPage = page;
    document.querySelectorAll('.page').forEach(p => p.classList.remove('active'));
    document.getElementById(`page-${page}`).classList.add('active');
    document.querySelectorAll('.nav-btn').forEach(b => b.classList.remove('active'));
    document.getElementById(`nav-${page}`).classList.add('active');
    window.scrollTo({ top: 0, behavior: 'smooth' });
}

// ========================================
// CATEGORIES
// ========================================
function renderCategories() {
    const container = document.getElementById('category-chips');
    const cats = Object.keys(state.categories).sort();

    // "Tümü" chip
    let html = `<button class="category-chip active" onclick="selectCategory(null)" id="chip-all">
        <span class="chip-dot" style="background: var(--accent)"></span>
        Tümü
        <span class="chip-count">${state.allParts.length}</span>
    </button>`;

    cats.forEach(cat => {
        const count = state.allParts.filter(p => p.kategori === cat).length;
        const color = getCategoryColor(cat);
        html += `<button class="category-chip" 
            onclick="selectCategory('${escapeHtml(cat)}')" 
            id="chip-${slugify(cat)}"
            style="--chip-color: ${color.color}; --chip-bg: ${color.bg};">
            <span class="chip-dot"></span>
            ${escapeHtml(cat)}
            <span class="chip-count">${count}</span>
        </button>`;
    });

    container.innerHTML = html;
}

function selectCategory(category) {
    state.selectedCategory = category;
    state.selectedSubcategory = null;

    // Update chip active states
    document.querySelectorAll('.category-chip').forEach(c => c.classList.remove('active'));
    if (category) {
        const chipEl = document.getElementById(`chip-${slugify(category)}`);
        if (chipEl) chipEl.classList.add('active');
    } else {
        document.getElementById('chip-all').classList.add('active');
    }

    // Show/hide subcategory section
    const subSection = document.getElementById('subcategory-section');
    if (category && state.categories[category] && state.categories[category].size > 0) {
        renderSubcategories(category);
        subSection.style.display = 'block';
    } else {
        subSection.style.display = 'none';
    }

    renderParts();
}

function renderSubcategories(category) {
    const container = document.getElementById('subcategory-chips');
    const subs = [...state.categories[category]].sort();
    const filteredParts = state.allParts.filter(p => p.kategori === category);

    let html = `<button class="subcategory-chip active" onclick="selectSubcategory(null)">
        Tümü (${filteredParts.length})
    </button>`;

    subs.forEach(sub => {
        const count = filteredParts.filter(p => p.altKategori === sub).length;
        html += `<button class="subcategory-chip" onclick="selectSubcategory('${escapeHtml(sub)}')">
            ${escapeHtml(sub)} (${count})
        </button>`;
    });

    container.innerHTML = html;
}

function selectSubcategory(sub) {
    state.selectedSubcategory = sub;
    document.querySelectorAll('.subcategory-chip').forEach(c => c.classList.remove('active'));
    if (sub) {
        // Find the chip by text content
        document.querySelectorAll('.subcategory-chip').forEach(c => {
            if (c.textContent.trim().startsWith(sub)) c.classList.add('active');
        });
    } else {
        document.querySelector('.subcategory-chip').classList.add('active');
    }
    renderParts();
}

// ========================================
// RENDER PARTS
// ========================================
function renderParts() {
    const filtered = getFilteredParts();
    const grid = document.getElementById('parts-grid');
    const noResults = document.getElementById('no-results');
    const resultCount = document.getElementById('result-count');

    resultCount.textContent = `${filtered.length} parça`;

    if (filtered.length === 0) {
        grid.innerHTML = '';
        noResults.style.display = 'block';
        return;
    }

    noResults.style.display = 'none';

    grid.innerHTML = filtered.map((part, idx) => {
        const color = getCategoryColor(part.kategori);
        const delay = Math.min(idx * 0.03, 0.5);
        const isFav = state.favorites.includes(part.No);
        const favIcon = '<path d="M20.84 4.61a5.5 5.5 0 0 0-7.78 0L12 5.67l-1.06-1.06a5.5 5.5 0 0 0-7.78 7.78l1.06 1.06L12 21.23l7.78-7.78 1.06-1.06a5.5 5.5 0 0 0 0-7.78z"></path>';
        
        return `<div class="part-card" onclick="openModal(${part.No})" style="animation-delay: ${delay}s; --card-accent: ${color.color}">
            <button class="part-favorite-icon ${isFav ? 'active' : ''}" data-part-id="${part.No}" onclick="toggleFavorite(event, ${part.No})" title="${isFav ? 'Favorilerden Çıkar' : 'Favorilere Ekle'}">
                <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">${favIcon}</svg>
            </button>
            <div class="part-card-header">
                <span class="part-name">${highlight(part.parcaAdi, state.searchQuery)}</span>
                <span class="part-rank">#${part.No}</span>
            </div>
            <div class="part-tags">
                <span class="part-tag category" style="--tag-bg: ${color.bg}; --tag-color: ${color.color};">${escapeHtml(part.kategori)}</span>
                ${part.altKategori ? `<span class="part-tag subcategory">${escapeHtml(part.altKategori)}</span>` : ''}
            </div>
            <div class="part-description">${highlight(truncate(part.gorevi, 120), state.searchQuery)}</div>
            <div class="part-meta">
                <div class="part-meta-item">
                    <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M19 21l-7-5-7 5V5a2 2 0 0 1 2-2h10a2 2 0 0 1 2 2z"/></svg>
                    ${escapeHtml(part.aracTipi)}
                </div>
                <div class="part-meta-item">
                    <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"/><polyline points="12 6 12 12 16 14"/></svg>
                    ${escapeHtml(truncate(part.degisimPeriyodu, 30))}
                </div>
            </div>
        </div>`;
    }).join('');
}

function getFilteredParts() {
    let parts = [...state.allParts];

    // Category filter
    if (state.selectedCategory) {
        parts = parts.filter(p => p.kategori === state.selectedCategory);
    }

    // Subcategory filter
    if (state.selectedSubcategory) {
        parts = parts.filter(p => p.altKategori === state.selectedSubcategory);
    }

    // Search filter
    if (state.searchQuery) {
        const query = state.searchQuery.toLowerCase();
        const terms = query.split(/\s+/);
        parts = parts.filter(p => {
            const searchText = [
                p.parcaAdi,
                p.kategori,
                p.altKategori,
                p.gorevi,
                p.kullanimYeri,
                p.aracTipi,
                p.degisimPeriyodu,
                p.onemliNotlar
            ].join(' ').toLowerCase();
            return terms.every(t => searchText.includes(t));
        });
    }

    return parts;
}

// ========================================
// RANKING PAGE
// ========================================
function renderRanking() {
    const container = document.getElementById('ranking-list');
    let parts = [...state.allParts].sort((a, b) => a.No - b.No);

    // Apply ranking search filter
    if (state.rankingSearchQuery) {
        const query = state.rankingSearchQuery.toLowerCase();
        const terms = query.split(/\s+/);
        parts = parts.filter(p => {
            const searchText = [p.parcaAdi, p.kategori, p.altKategori, p.aracTipi].join(' ').toLowerCase();
            return terms.every(t => searchText.includes(t));
        });
    }

    if (parts.length === 0) {
        container.innerHTML = `<div class="no-results">
            <svg width="64" height="64" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5">
                <circle cx="11" cy="11" r="8"/><line x1="21" y1="21" x2="16.65" y2="16.65"/>
            </svg>
            <h3>Sonuç bulunamadı</h3>
        </div>`;
        return;
    }

    container.innerHTML = parts.map(part => {
        const color = getCategoryColor(part.kategori);
        let rankClass = 'normal';
        if (part.No <= 3) rankClass = 'top-3';
        else if (part.No <= 10) rankClass = 'top-10';

        return `<div class="ranking-item" onclick="openModal(${part.No})">
            <div class="ranking-number ${rankClass}">${part.No}</div>
            <div class="ranking-info">
                <div class="ranking-part-name">${highlight(part.parcaAdi, state.rankingSearchQuery)}</div>
                <div class="ranking-part-meta">
                    <span class="ranking-part-category" style="--tag-bg: ${color.bg}; --tag-color: ${color.color};">${escapeHtml(part.kategori)}</span>
                    ${part.altKategori ? `<span class="ranking-part-subcat">• ${escapeHtml(part.altKategori)}</span>` : ''}
                </div>
            </div>
            <div class="ranking-part-vehicle">
                <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                    <path d="M19 21l-7-5-7 5V5a2 2 0 0 1 2-2h10a2 2 0 0 1 2 2z"/>
                </svg>
                ${escapeHtml(part.aracTipi)}
            </div>
        </div>`;
    }).join('');
}

// ========================================
// MODAL
// ========================================
function openModal(partNo) {
    const part = state.allParts.find(p => p.No === partNo);
    if (!part) return;

    const color = getCategoryColor(part.kategori);
    const content = document.getElementById('modal-content');

    content.innerHTML = `
        <div class="modal-badge" style="background: ${color.bg}; color: ${color.color};">
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="23 6 13.5 15.5 8.5 10.5 1 18"/><polyline points="17 6 23 6 23 12"/></svg>
            Talep Sırası: #${part.No}
        </div>
        <h2 class="modal-title">${escapeHtml(part.parcaAdi)}</h2>
        <div class="modal-tags">
            <span class="part-tag category" style="--tag-bg: ${color.bg}; --tag-color: ${color.color};">${escapeHtml(part.kategori)}</span>
            ${part.altKategori ? `<span class="part-tag subcategory">${escapeHtml(part.altKategori)}</span>` : ''}
        </div>
        <div class="modal-description">${escapeHtml(part.gorevi)}</div>
        <div class="modal-details">
            ${modalDetailRow('Kullanım Yeri', part.kullanimYeri, '<path d="M21 10c0 7-9 13-9 13s-9-6-9-13a9 9 0 0 1 18 0z"/><circle cx="12" cy="10" r="3"/>')}
            ${modalDetailRow('Araç Tipi', part.aracTipi, '<path d="M19 21l-7-5-7 5V5a2 2 0 0 1 2-2h10a2 2 0 0 1 2 2z"/>')}
            ${modalDetailRow('Değişim Periyodu', part.degisimPeriyodu, '<circle cx="12" cy="12" r="10"/><polyline points="12 6 12 12 16 14"/>')}
        </div>
        ${part.onemliNotlar ? `
        <div class="modal-note">
            <div class="modal-note-icon">
                <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                    <path d="M10.29 3.86L1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z"/>
                    <line x1="12" y1="9" x2="12" y2="13"/><line x1="12" y1="17" x2="12.01" y2="17"/>
                </svg>
            </div>
            <div class="modal-note-text"><strong>Önemli Not:</strong> ${escapeHtml(part.onemliNotlar)}</div>
        </div>` : ''}
        <div class="modal-actions">
            <button class="btn-favorite ${state.favorites.includes(part.No) ? 'active' : ''}" onclick="toggleFavorite(null, ${part.No}); openModal(${part.No})">
                <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M20.84 4.61a5.5 5.5 0 0 0-7.78 0L12 5.67l-1.06-1.06a5.5 5.5 0 0 0-7.78 7.78l1.06 1.06L12 21.23l7.78-7.78 1.06-1.06a5.5 5.5 0 0 0 0-7.78z"></path></svg>
                ${state.favorites.includes(part.No) ? 'Favorilerden Çıkar' : 'Favorilere Ekle'}
            </button>
        </div>
    `;

    document.getElementById('modal-overlay').classList.add('active');
    document.body.style.overflow = 'hidden';
}

function modalDetailRow(label, value, iconPath) {
    if (!value) return '';
    return `<div class="modal-detail-row">
        <div class="modal-detail-icon">
            <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">${iconPath}</svg>
        </div>
        <div class="modal-detail-content">
            <div class="modal-detail-label">${label}</div>
            <div class="modal-detail-value">${escapeHtml(value)}</div>
        </div>
    </div>`;
}

function closeModal() {
    document.getElementById('modal-overlay').classList.remove('active');
    document.body.style.overflow = '';
}

// ========================================
// STATS
// ========================================
function updateStats() {
    const cats = Object.keys(state.categories).length;
    document.getElementById('header-stats').textContent = `${state.allParts.length} parça • ${cats} kategori`;
}

// ========================================
// UTILITY FUNCTIONS
// ========================================
function clearSearch() {
    document.getElementById('search-input').value = '';
    state.searchQuery = '';
    document.getElementById('search-clear').style.display = 'none';
    renderParts();
}

function clearRankingSearch() {
    document.getElementById('ranking-search-input').value = '';
    state.rankingSearchQuery = '';
    document.getElementById('ranking-search-clear').style.display = 'none';
    renderRanking();
}

function debounce(fn, delay) {
    let timer;
    return (...args) => {
        clearTimeout(timer);
        timer = setTimeout(() => fn(...args), delay);
    };
}

function escapeHtml(text) {
    if (!text) return '';
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}

function highlight(text, query) {
    if (!query || !text) return text;
    const escaped = query.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
    const terms = escaped.split(/\s+/).filter(Boolean);
    let result = text;
    terms.forEach(term => {
        const regex = new RegExp(`(${term})`, 'gi');
        result = result.replace(regex, '<mark style="background:#fef08a;color:inherit;padding:0 1px;border-radius:2px;">$1</mark>');
    });
    return result;
}

function truncate(text, maxLen) {
    if (!text) return '';
    if (text.length <= maxLen) return text;
    return text.substring(0, maxLen).trim() + '...';
}

function slugify(text) {
    return text.toLowerCase()
        .replace(/[çÇ]/g, 'c')
        .replace(/[ğĞ]/g, 'g')
        .replace(/[ıİ]/g, 'i')
        .replace(/[öÖ]/g, 'o')
        .replace(/[şŞ]/g, 's')
        .replace(/[üÜ]/g, 'u')
        .replace(/[^a-z0-9]+/g, '-')
        .replace(/^-+|-+$/g, '');
}

// ========================================
// NEW FEATURES LOGIC
// ========================================

function applyTheme() {
    if (state.theme === 'dark') {
        document.documentElement.setAttribute('data-theme', 'dark');
        document.getElementById('theme-icon').innerHTML = '<path d="M12 3v1m0 16v1m9-9h-1M4 12H3m15.364 6.364l-.707-.707M6.343 6.343l-.707-.707m12.728 0l-.707.707M6.343 17.657l-.707.707M16 12a4 4 0 11-8 0 4 4 0 018 0z"></path>';
    } else {
        document.documentElement.removeAttribute('data-theme');
        document.getElementById('theme-icon').innerHTML = '<path d="M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z"></path>';
    }
}

function toggleTheme() {
    // If the browser supports View Transitions (Modern Chrome/Edge)
    if (document.startViewTransition) {
        document.startViewTransition(() => {
            document.body.classList.add('disable-transitions');
            state.theme = state.theme === 'light' ? 'dark' : 'light';
            localStorage.setItem('theme', state.theme);
            applyTheme();
            document.body.classList.remove('disable-transitions');
        });
    } else {
        // Fallback for older browsers: instant theme switch to prevent lag
        document.body.classList.add('disable-transitions');
        state.theme = state.theme === 'light' ? 'dark' : 'light';
        localStorage.setItem('theme', state.theme);
        applyTheme();
        void document.body.offsetHeight;
        document.body.classList.remove('disable-transitions');
    }
}

function toggleFavorite(event, partNo) {
    if (event) {
        event.preventDefault();
        event.stopPropagation();
    }
    
    const index = state.favorites.indexOf(partNo);
    const isNowFav = index === -1;
    
    if (isNowFav) {
        state.favorites.push(partNo);
    } else {
        state.favorites.splice(index, 1);
    }
    
    // Save to localStorage
    localStorage.setItem('favorites', JSON.stringify(state.favorites));
    
    // Update DOM of the part cards directly
    const buttons = document.querySelectorAll(`.part-favorite-icon[data-part-id="${partNo}"]`);
    buttons.forEach(btn => {
        if (isNowFav) {
            btn.classList.add('active');
            btn.title = 'Favorilerden Çıkar';
        } else {
            btn.classList.remove('active');
            btn.title = 'Favorilere Ekle';
        }
    });
    
    // Update favorites list
    if (state.currentPage === 'favorites') {
        renderFavorites();
    } else {
        // Just update in memory/background, we don't need to rebuild all cards
        renderFavorites();
    }
}

function renderFavorites() {
    const grid = document.getElementById('favorites-grid');
    const emptyState = document.getElementById('favorites-empty');
    if (!grid || !emptyState) return;

    // Filter parts that are in favorites
    const favParts = state.allParts.filter(p => state.favorites.includes(p.No));

    if (favParts.length === 0) {
        grid.innerHTML = '';
        emptyState.style.display = 'flex';
        emptyState.style.flexDirection = 'column';
        emptyState.style.alignItems = 'center';
        return;
    }

    emptyState.style.display = 'none';

    grid.innerHTML = favParts.map((part, idx) => {
        const color = getCategoryColor(part.kategori);
        const delay = Math.min(idx * 0.03, 0.5);
        return `<div class="part-card" onclick="openModal(${part.No})" style="animation-delay: ${delay}s; --card-accent: ${color.color}">
            <button class="part-favorite-icon active" data-part-id="${part.No}" onclick="toggleFavorite(event, ${part.No})" title="Favorilerden Çıkar">
                <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M20.84 4.61a5.5 5.5 0 0 0-7.78 0L12 5.67l-1.06-1.06a5.5 5.5 0 0 0-7.78 7.78l1.06 1.06L12 21.23l7.78-7.78 1.06-1.06a5.5 5.5 0 0 0 0-7.78z"></path></svg>
            </button>
            <div class="part-card-header">
                <span class="part-name">${escapeHtml(part.parcaAdi)}</span>
                <span class="part-rank">#${part.No}</span>
            </div>
            <div class="part-tags">
                <span class="part-tag category" style="--tag-bg: ${color.bg}; --tag-color: ${color.color};">${escapeHtml(part.kategori)}</span>
            </div>
            <div class="part-meta">
                <div class="part-meta-item">
                    <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M19 21l-7-5-7 5V5a2 2 0 0 1 2-2h10a2 2 0 0 1 2 2z"/></svg>
                    ${escapeHtml(part.aracTipi)}
                </div>
            </div>
        </div>`;
    }).join('');
}
