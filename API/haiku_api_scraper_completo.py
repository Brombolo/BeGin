import os
import requests
from bs4 import BeautifulSoup
from urllib.parse import urljoin, urlparse, unquote
import re

# URL iniziale della documentazione delle API di Haiku OS
START_URL = "https://www.haiku-os.org/docs/api/"
ALLOWED_DOMAIN = urlparse(START_URL).netloc

# Cartella locale dove verranno salvate le immagini scaricate
IMAGE_DIR = "haiku_api_images"

# Insieme per evitare di scaricare o scansionare due volte la stessa risorsa
visited_urls = set()
downloaded_images = set()

def sanitize_filename(url):
    """Genera un nome file sicuro e pulito partendo dall'URL dell'immagine."""
    path = urlparse(url).path
    filename = os.path.basename(path)
    if not filename:
        filename = "immagine_senza_nome.png"
    # Rimuove caratteri non validi per i file system
    filename = unquote(filename)
    filename = re.sub(r'[^a-zA-Z0-9_\\.-]', '_', filename)
    return filename

def download_image_locally(img_url):
    """Scarica fisicamente l'immagine sul PC e restituisce il percorso relativo."""
    if img_url in downloaded_images:
        # Se l'abbiamo già scaricata, restituiamo il suo percorso senza riscaricarla
        return os.path.join(IMAGE_DIR, sanitize_filename(img_url))
        
    try:
        # Creiamo la cartella delle immagini se non esiste
        if not os.path.exists(IMAGE_DIR):
            os.makedirs(IMAGE_DIR)
            
        filename = sanitize_filename(img_url)
        local_path = os.path.join(IMAGE_DIR, filename)
        
        # Evitiamo sovrascritture se immagini diverse hanno lo stesso nome file finale
        counter = 1
        base_name, ext = os.path.splitext(filename)
        while os.path.exists(local_path) and img_url not in downloaded_images:
            local_path = os.path.join(IMAGE_DIR, f"{base_name}_{counter}{ext}")
            counter += 1
            
        headers = {'User-Agent': 'HaikuDocsScraper/1.0 (Community Project)'}
        response = requests.get(img_url, headers=headers, timeout=15)
        if response.status_code == 200:
            with open(local_path, "wb") as f:
                f.write(response.content)
            downloaded_images.add(img_url)
            print(f"  [➔] Immagine scaricata: {local_path}")
            return local_path
    except Exception as e:
        print(f"  [!] Errore nel download dell'immagine {img_url}: {e}")
        
    return None

def get_page_content(url):
    try:
        headers = {'User-Agent': 'HaikuDocsScraper/1.0 (Community Project)'}
        response = requests.get(url, headers=headers, timeout=15)
        if response.status_code == 200:
            return BeautifulSoup(response.text, 'html.parser')
    except Exception as e:
        print(f"[-] Errore durante il download di {url}: {e}")
    return None

def extract_page_elements(soup, current_url):
    elements_data = []
    main_content = soup.find('main') or soup.find('article') or soup.find('div', class_='content') or soup
    
    for element in main_content.find_all(['h1', 'h2', 'h3', 'h4', 'p', 'img', 'ul', 'ol', 'pre']):
        
        if element.name in ['h1', 'h2', 'h3', 'h4']:
            level = int(element.name[1])
            text = element.get_text(strip=True)
            if text:
                elements_data.append({"type": "heading", "level": level, "text": text})
                
        elif element.name == 'pre':
            code_text = element.get_text()
            if code_text.strip():
                elements_data.append({"type": "code", "text": code_text})
                
        elif element.name in ['p', 'ul', 'ol']:
            if element.name in ['ul', 'ol']:
                items = [li.get_text(strip=True) for li in element.find_all('li') if li.get_text(strip=True)]
                if items:
                    elements_data.append({"type": "list", "style": element.name, "items": items})
            else:
                text = element.get_text(strip=True)
                if text:
                    elements_data.append({"type": "text", "text": text})
                    
        elif element.name == 'img':
            src = element.get('src')
            if src:
                # Converte l'URL in assoluto (funziona sia per domini interni che esterni)
                full_img_url = urljoin(current_url, src)
                alt_text = element.get('alt', 'Immagine documentazione')
                
                # SCARICAMENTO LOCALE DELL'IMMAGINE
                print(f"  [..] Rilevata immagine: {full_img_url}. Avvio download...")
                local_path = download_image_locally(full_img_url)
                
                if local_path:
                    # Inseriamo nello script il percorso relativo locale (es: haiku_api_images/diagramma.png)
                    elements_data.append({"type": "image", "url": local_path, "alt": alt_text})
                else:
                    # Fallback sul link remoto in caso di errore di download
                    elements_data.append({"type": "image", "url": full_img_url, "alt": alt_text})
                
    return elements_data

def crawl_hierarchy(url, depth=0):
    if url in visited_urls:
        return []
        
    parsed_url = urlparse(url)
    if parsed_url.netloc != ALLOWED_DOMAIN or not parsed_url.path.startswith("/docs/api/"):
        return []
        
    print(f"{'  ' * depth}[+] Analisi in corso: {url}")
    visited_urls.add(url)
    
    soup = get_page_content(url)
    if not soup:
        return []
        
    current_content = extract_page_elements(soup, url)
    child_contents = []
    
    for link in soup.find_all('a', href=True):
        href = link['href']
        full_url = urljoin(url, href).split('#')[0]
        
        if full_url not in visited_urls:
            if urlparse(full_url).netloc == ALLOWED_DOMAIN and urlparse(full_url).path.startswith("/docs/api/"):
                child_data = crawl_hierarchy(full_url, depth + 1)
                child_contents.extend(child_data)
                
    return current_content + child_contents

def convert_to_markdown(data, output_filename="documentazione_haiku_api.md"):
    print(f"\n[*] Scrittura del file Markdown unico...")
    with open(output_filename, "w", encoding="utf-8") as f:
        f.write("# Documentazione API Haiku OS\n\n")
        f.write("> File unico generato offline con asset locali.\n\n---\n\n")
        
        for item in data:
            if item["type"] == "heading":
                hashes = "#" * (item["level"] + 1)
                f.write(f"{hashes} {item['text']}\n\n")
            elif item["type"] == "code":
                f.write(f"```cpp\n{item['text']}\n```\n\n")
            elif item["type"] == "text":
                f.write(f"{item['text']}\n\n")
            elif item["type"] == "list":
                for i, li_text in enumerate(item["items"], 1):
                    if item["style"] == "ol":
                        f.write(f"{i}. {li_text}\n")
                    else:
                        f.write(f"* {li_text}\n")
                f.write("\n")
            elif item["type"] == "image":
                # Scrive il link del Markdown puntando alla cartella locale delle immagini
                f.write(f"![{item['alt']}]({item['url']})\n\n")
                
    print(f"[+] Completato! File Markdown generato: {os.path.abspath(output_filename)}")
    print(f"[+] Asset multimediali salvati nella cartella: {os.path.abspath(IMAGE_DIR)}")

if __name__ == "__main__":
    print("====================================================")
    print("  Scraper Ricorsivo con Download Locale Immagini")
    print("====================================================")
    
    scraped_data = crawl_hierarchy(START_URL)
    
    if scraped_data:
        convert_to_markdown(scraped_data)
    else:
        print("[-] Nessun dato estratto.")
